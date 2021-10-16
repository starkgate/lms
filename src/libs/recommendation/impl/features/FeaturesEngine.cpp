/*
 * Copyright (C) 2018 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "FeaturesEngine.hpp"

#include <numeric>

#include "lmscore/database/Artist.hpp"
#include "lmscore/database/Db.hpp"
#include "lmscore/database/Release.hpp"
#include "lmscore/database/Session.hpp"
#include "lmscore/database/Track.hpp"
#include "lmscore/database/TrackArtistLink.hpp"
#include "lmscore/database/TrackFeatures.hpp"
#include "lmscore/database/TrackList.hpp"
#include "som/DataNormalizer.hpp"
#include "utils/Logger.hpp"
#include "utils/Random.hpp"


namespace Recommendation {

std::unique_ptr<IEngine> createFeaturesEngine(Database::Db& db)
{
	return std::make_unique<FeaturesEngine>(db);
}

const FeatureSettingsMap&
FeaturesEngine::getDefaultTrainFeatureSettings()
{
	static const FeatureSettingsMap defaultTrainFeatureSettings
	{
		{ "lowlevel.spectral_energyband_high.mean",	{1}},
		{ "lowlevel.spectral_rolloff.median",		{1}},
		{ "lowlevel.spectral_contrast_valleys.var",	{1}},
		{ "lowlevel.erbbands.mean",			{1}},
		{ "lowlevel.gfcc.mean",				{1}},
	};

	return defaultTrainFeatureSettings;
}

static
std::optional<FeatureValuesMap>
getTrackFeatureValues(FeaturesEngine::FeaturesFetchFunc func, Database::TrackId trackId, const std::unordered_set<FeatureName>& featureNames)
{
	return func(trackId, featureNames);
}

static
std::optional<FeatureValuesMap>
getTrackFeatureValuesFromDb(Database::Session& session, Database::TrackId trackId, const std::unordered_set<FeatureName>& featureNames)
{
	auto func = [&](Database::TrackId trackId, const std::unordered_set<FeatureName>& featureNames)
	{
		std::optional<FeatureValuesMap> res;

		auto transaction {session.createSharedTransaction()};

		Database::Track::pointer track {Database::Track::getById(session, trackId)};
		if (!track)
			return res;

		res = track->getTrackFeatures()->getFeatureValuesMap(featureNames);
		if (res->empty())
			res.reset();

		return res;
	};

	return getTrackFeatureValues(func, trackId, featureNames);
}

static
std::optional<SOM::InputVector>
convertFeatureValuesMapToInputVector(const FeatureValuesMap& featureValuesMap, std::size_t nbDimensions)
{
	std::size_t i {};
	std::optional<SOM::InputVector> res {SOM::InputVector {nbDimensions}};
	for (const auto& [featureName, values] : featureValuesMap)
	{
		if (values.size() != getFeatureDef(featureName).nbDimensions)
		{
			LMS_LOG(RECOMMENDATION, WARNING) << "Dimension mismatch for feature '" << featureName << "'. Expected " << getFeatureDef(featureName).nbDimensions << ", got " << values.size();
			res.reset();
			break;
		}

		for (double val : values)
			(*res)[i++] = val;
	}

	return res;
}

static
SOM::InputVector
getInputVectorWeights(const FeatureSettingsMap& featureSettingsMap, std::size_t nbDimensions)
{
	SOM::InputVector weights {nbDimensions};
	std::size_t index {};
	for (const auto& [featureName, featureSettings] : featureSettingsMap)
	{
		const std::size_t featureNbDimensions {getFeatureDef(featureName).nbDimensions};

		for (std::size_t i {}; i < featureNbDimensions; ++i)
			weights[index++] = (1. / featureNbDimensions * featureSettings.weight);
	}

	assert(index == nbDimensions);

	return weights;
}

void
FeaturesEngine::loadFromTraining(const TrainSettings& trainSettings, const ProgressCallback& progressCallback)
{
	LMS_LOG(RECOMMENDATION, INFO) << "Constructing features classifier...";

	std::unordered_set<FeatureName> featureNames;
	std::transform(std::cbegin(trainSettings.featureSettingsMap), std::cend(trainSettings.featureSettingsMap), std::inserter(featureNames, std::begin(featureNames)),
		[](const auto& itFeatureSetting) { return itFeatureSetting.first; });

	const std::size_t nbDimensions {std::accumulate(std::cbegin(featureNames), std::cend(featureNames), std::size_t {0},
			[](std::size_t sum, const FeatureName& featureName) { return sum + getFeatureDef(featureName).nbDimensions; })};

	LMS_LOG(RECOMMENDATION, DEBUG) << "Features dimension = " << nbDimensions;

	Database::Session& session {_db.getTLSSession()};

	std::vector<Database::TrackId> trackIds;
	{
		auto transaction {session.createSharedTransaction()};

		LMS_LOG(RECOMMENDATION, DEBUG) << "Getting Tracks with features...";
		trackIds = Database::Track::getAllIdsWithFeatures(session);
		LMS_LOG(RECOMMENDATION, DEBUG) << "Getting Tracks with features DONE (found " << trackIds.size() << " tracks)";
	}

	std::vector<SOM::InputVector> samples;
	std::vector<Database::TrackId> samplesTrackIds;

	samples.reserve(trackIds.size());
	samplesTrackIds.reserve(trackIds.size());

	LMS_LOG(RECOMMENDATION, DEBUG) << "Extracting features...";
	for (Database::TrackId trackId : trackIds)
	{
		if (_loadCancelled)
			return;

		std::optional<FeatureValuesMap> featureValuesMap;

		if (_featuresFetchFunc)
			featureValuesMap = getTrackFeatureValues(_featuresFetchFunc, trackId, featureNames);
		else
			featureValuesMap = getTrackFeatureValuesFromDb(session, trackId, featureNames);

		if (!featureValuesMap)
			continue;

		std::optional<SOM::InputVector> inputVector {convertFeatureValuesMapToInputVector(*featureValuesMap, nbDimensions)};
		if (!inputVector)
			continue;

		samples.emplace_back(std::move(*inputVector));
		samplesTrackIds.emplace_back(trackId);
	}
	LMS_LOG(RECOMMENDATION, DEBUG) << "Extracting features DONE";

	if (samples.empty())
	{
		LMS_LOG(RECOMMENDATION, INFO) << "Nothing to classify!";
		return;
	}

	LMS_LOG(RECOMMENDATION, DEBUG) << "Normalizing data...";
	SOM::DataNormalizer dataNormalizer {nbDimensions};

	dataNormalizer.computeNormalizationFactors(samples);
	for (auto& sample : samples)
		dataNormalizer.normalizeData(sample);

	SOM::Coordinate size {static_cast<SOM::Coordinate>(std::sqrt(samples.size() / trainSettings.sampleCountPerNeuron))};
	if (size < 2)
	{
		LMS_LOG(RECOMMENDATION, WARNING) << "Very few tracks (" << samples.size() << ") are being used by the features engine, expect bad behaviors";
		size = 2;
	}
	LMS_LOG(RECOMMENDATION, INFO) << "Found " << samples.size() << " tracks, constructing a " << size << "*" << size << " network";

	SOM::Network network {size, size, nbDimensions};

	SOM::InputVector weights {getInputVectorWeights(trainSettings.featureSettingsMap, nbDimensions)};
	network.setDataWeights(weights);

	auto somProgressCallback{[&](const SOM::Network::CurrentIteration& iter)
	{
		LMS_LOG(RECOMMENDATION, DEBUG) << "Current pass = " << iter.idIteration << " / " << iter.iterationCount;
		progressCallback(Progress {iter.idIteration, iter.iterationCount});
	}};

	LMS_LOG(RECOMMENDATION, DEBUG) << "Training network...";
	network.train(samples, trainSettings.iterationCount,
			progressCallback ? somProgressCallback : SOM::Network::ProgressCallback {},
			[this] { return _loadCancelled; });
	LMS_LOG(RECOMMENDATION, DEBUG) << "Training network DONE";


	LMS_LOG(RECOMMENDATION, DEBUG) << "Classifying tracks...";
	TrackPositions trackPositions;
	for (std::size_t i {}; i < samples.size(); ++i)
	{
		if (_loadCancelled)
			return;

		const SOM::Position position {network.getClosestRefVectorPosition(samples[i])};

		trackPositions[samplesTrackIds[i]].push_back(position);
	}

	LMS_LOG(RECOMMENDATION, DEBUG) << "Classifying tracks DONE";

	load(std::move(network), std::move(trackPositions));
}

void
FeaturesEngine::loadFromCache(FeaturesEngineCache cache)
{
	LMS_LOG(RECOMMENDATION, INFO) << "Constructing features classifier from cache...";

	load(std::move(cache._network), cache._trackPositions);
}

IEngine::TrackContainer
FeaturesEngine::getSimilarTracksFromTrackList(Database::TrackListId trackListId, std::size_t maxCount) const
{
	const TrackContainer trackIds {[&]
	{
		TrackContainer res;

		Database::Session& session {_db.getTLSSession()};

		auto transaction {session.createSharedTransaction()};

		const Database::TrackList::pointer trackList {Database::TrackList::getById(session, trackListId)};
		if (trackList)
			res = trackList->getTrackIds();

		return res;
	}()};

	return getSimilarTracks(trackIds, maxCount);
}

IEngine::TrackContainer
FeaturesEngine::getSimilarTracks(const std::vector<Database::TrackId>& tracksIds, std::size_t maxCount) const
{
	auto similarTrackIds {getSimilarObjects(tracksIds, _trackMatrix, _trackPositions, maxCount)};

	Database::Session& session {_db.getTLSSession()};

	{
		// Report only existing ids, as tracks may have been removed a long time ago (refreshing the SOM takes some time)
		auto transaction {session.createSharedTransaction()};

		similarTrackIds.erase(std::remove_if(std::begin(similarTrackIds), std::end(similarTrackIds),
			[&](Database::TrackId trackId)
			{
				return !Database::Track::exists(session, trackId);
			}), std::end(similarTrackIds));
	}

	return similarTrackIds;
}

IEngine::ReleaseContainer
FeaturesEngine::getSimilarReleases(Database::ReleaseId releaseId, std::size_t maxCount) const
{
	auto similarReleaseIds {getSimilarObjects({releaseId}, _releaseMatrix, _releasePositions, maxCount)};

	Database::Session& session {_db.getTLSSession()};

	if (!similarReleaseIds.empty())
	{
		// Report only existing ids
		auto transaction {session.createSharedTransaction()};

		similarReleaseIds.erase(std::remove_if(std::begin(similarReleaseIds), std::end(similarReleaseIds),
			[&](Database::ReleaseId releaseId)
			{
				return !Database::Release::exists(session, releaseId);
			}), std::end(similarReleaseIds));
	}

	return similarReleaseIds;
}

std::vector<Database::ArtistId>
FeaturesEngine::getSimilarArtists(Database::ArtistId artistId, EnumSet<Database::TrackArtistLinkType> linkTypes, std::size_t maxCount) const
{
	auto getSimilarArtistIdsForLinkType {[&] (Database::TrackArtistLinkType linkType)
	{
		std::vector<Database::ArtistId> similarArtistIds;

		const auto itArtists {_artistMatrix.find(linkType)};
		if (itArtists == std::cend(_artistMatrix))
		{
			return similarArtistIds;
		}

		return getSimilarObjects({artistId}, itArtists->second, _artistPositions, maxCount);
	}};

	std::unordered_set<Database::ArtistId> similarArtistIds;

	for (Database::TrackArtistLinkType linkType : linkTypes)
	{
		const auto similarArtistIdsForLinkType {getSimilarArtistIdsForLinkType(linkType)};
		similarArtistIds.insert(std::begin(similarArtistIdsForLinkType), std::end(similarArtistIdsForLinkType));
	}

	std::vector<Database::ArtistId> res(std::cbegin(similarArtistIds), std::cend(similarArtistIds));

	Database::Session& session {_db.getTLSSession()};
	{
		// Report only existing ids
		auto transaction {session.createSharedTransaction()};

		res.erase(std::remove_if(std::begin(res), std::end(res),
			[&](Database::ArtistId artistId)
			{
				return !Database::Artist::exists(session, artistId);
			}), std::end(res));
	}

	while (res.size() > maxCount)
		res.erase(Random::pickRandom(res));

	return res;
}

FeaturesEngineCache
FeaturesEngine::toCache() const
{
	return FeaturesEngineCache {*_network, _trackPositions};
}

void
FeaturesEngine::load(bool forceReload, const ProgressCallback& progressCallback)
{
	if (forceReload)
	{
		FeaturesEngineCache::invalidate();
	}
	else if (const std::optional<FeaturesEngineCache> cache {FeaturesEngineCache::read()})
	{
		loadFromCache(*cache);
		return;
	}

	TrainSettings trainSettings;
	trainSettings.featureSettingsMap = getDefaultTrainFeatureSettings();

	loadFromTraining(trainSettings, progressCallback);
	if (!_loadCancelled)
		toCache().write();
}

void
FeaturesEngine::requestCancelLoad()
{
	LMS_LOG(RECOMMENDATION, DEBUG) << "Requesting init cancellation";
	_loadCancelled = true;
}

void
FeaturesEngine::load(const SOM::Network& network, const TrackPositions& trackPositions)
{
	using namespace Database;

	_networkRefVectorsDistanceMedian = network.computeRefVectorsDistanceMedian();
	LMS_LOG(RECOMMENDATION, DEBUG) << "Median distance betweend ref vectors = " << _networkRefVectorsDistanceMedian;

	const SOM::Coordinate width {network.getWidth()};
	const SOM::Coordinate height {network.getHeight()};

	_releaseMatrix = ReleaseMatrix {width, height};
	_trackMatrix = TrackMatrix {width, height};

	LMS_LOG(RECOMMENDATION, DEBUG) << "Constructing maps...";

	Database::Session& session {_db.getTLSSession()};

	for (const auto& [trackId, positions] : trackPositions)
	{
		if (_loadCancelled)
			return;

		auto transaction {session.createSharedTransaction()};

		const Track::pointer track {Database::Track::getById(session, trackId)};
		if (!track)
			continue;

		for (const SOM::Position& position : positions)
		{
			Utils::push_back_if_not_present(_trackPositions[trackId], position);
			Utils::push_back_if_not_present(_trackMatrix[position], trackId);

			if (Release::pointer release {track->getRelease()})
			{
				const ReleaseId releaseId {release->getId()};
				Utils::push_back_if_not_present(_releasePositions[releaseId], position);
				Utils::push_back_if_not_present(_releaseMatrix[position], releaseId);
			}
			for (const TrackArtistLink::pointer& artistLink : track->getArtistLinks())
			{
				const ArtistId artistId {artistLink->getArtist()->getId()};

				Utils::push_back_if_not_present(_artistPositions[artistId], position);
				auto itArtists {_artistMatrix.find(artistLink->getType())};
				if (itArtists == std::cend(_artistMatrix))
				{
					auto [it, inserted] = _artistMatrix.try_emplace(artistLink->getType(), ArtistMatrix {width, height});
					assert(inserted);
					itArtists = it;
				}
				Utils::push_back_if_not_present(itArtists->second[position], artistId);
			}
		}
	}

	_network = std::make_unique<SOM::Network>(network);

	LMS_LOG(RECOMMENDATION, INFO) << "Classifier successfully loaded!";
}

} // ns Recommendation
