/*
 * Copyright (C) 2015 Emeric Poupon
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

#include "AudioTranscodingResource.hpp"

#include <optional>
#include <Wt/Http/Response.h>

#include "av/TranscodingParameters.hpp"
#include "av/TranscodingResourceHandlerCreator.hpp"
#include "av/Types.hpp"
#include "services/database/Session.hpp"
#include "services/database/Track.hpp"
#include "services/database/User.hpp"
#include "utils/Logger.hpp"
#include "utils/String.hpp"

#include "LmsApplication.hpp"

#define LOG(level)	LMS_LOG(UI, level) << "Audio transcode resource: "

namespace StringUtils
{
    template <>
    std::optional<Database::TranscodingOutputFormat> readAs(std::string_view str)
    {
        auto encodedFormat{ readAs<int>(str) };
        if (!encodedFormat)
            return std::nullopt;

        Database::TranscodingOutputFormat format{ static_cast<Database::TranscodingOutputFormat>(*encodedFormat) };

        // check
        switch (static_cast<Database::TranscodingOutputFormat>(*encodedFormat))
        {
        case Database::TranscodingOutputFormat::MP3:
            [[fallthrough]];
        case Database::TranscodingOutputFormat::OGG_OPUS:
            [[fallthrough]];
        case Database::TranscodingOutputFormat::MATROSKA_OPUS:
            [[fallthrough]];
        case Database::TranscodingOutputFormat::OGG_VORBIS:
            [[fallthrough]];
        case Database::TranscodingOutputFormat::WEBM_VORBIS:
            return format;
        }

        LOG(ERROR) << "Cannot determine audio format from value '" << str << "'";

        return std::nullopt;
    }
}

namespace UserInterface
{
    namespace
    {
        std::optional<Av::Transcoding::OutputFormat> AudioFormatToAvFormat(Database::TranscodingOutputFormat format)
        {
            switch (format)
            {
            case Database::TranscodingOutputFormat::MP3:			return Av::Transcoding::OutputFormat::MP3;
            case Database::TranscodingOutputFormat::OGG_OPUS:		return Av::Transcoding::OutputFormat::OGG_OPUS;
            case Database::TranscodingOutputFormat::MATROSKA_OPUS:	return Av::Transcoding::OutputFormat::MATROSKA_OPUS;
            case Database::TranscodingOutputFormat::OGG_VORBIS:		return Av::Transcoding::OutputFormat::OGG_VORBIS;
            case Database::TranscodingOutputFormat::WEBM_VORBIS:	return Av::Transcoding::OutputFormat::WEBM_VORBIS;
            }

            LOG(ERROR) << "Cannot convert from audio format to AV format";

            return std::nullopt;
        }

        template<typename T>
        std::optional<T> readParameterAs(const Wt::Http::Request& request, const std::string& parameterName)
        {
            auto paramStr{ request.getParameter(parameterName) };
            if (!paramStr)
            {
                LOG(DEBUG) << "Missing parameter '" << parameterName << "'";
                return std::nullopt;
            }

            auto res{ StringUtils::readAs<T>(*paramStr) };
            if (!res)
                LOG(ERROR) << "Cannot parse parameter '" << parameterName << "' from value '" << *paramStr << "'";

            return res;
        }

        struct TranscodingParameters
        {
            Av::Transcoding::InputParameters inputParameters;
            Av::Transcoding::OutputParameters outputParameters;
        };

        std::optional<TranscodingParameters> readTranscodingParameters(const Wt::Http::Request& request)
        {
            TranscodingParameters parameters;

            // mandatory parameters
            const std::optional<Database::TrackId> trackId{ readParameterAs<Database::TrackId::ValueType>(request, "trackid") };
            const auto format{ readParameterAs<Database::TranscodingOutputFormat>(request, "format") };
            const auto bitrate{ readParameterAs<Database::Bitrate>(request, "bitrate") };

            if (!trackId || !format || !bitrate)
                return std::nullopt;

            if (!Database::isAudioBitrateAllowed(*bitrate))
            {
                LOG(ERROR) << "Bitrate '" << *bitrate << "' is not allowed";
                return std::nullopt;
            }

            const std::optional<Av::Transcoding::OutputFormat> avFormat{ AudioFormatToAvFormat(*format) };
            if (!avFormat)
                return std::nullopt;

            // optional parameter
            std::size_t offset{ readParameterAs<std::size_t>(request, "offset").value_or(0) };

            std::filesystem::path trackPath;
            {
                auto transaction{ LmsApp->getDbSession().createSharedTransaction() };

                const Database::Track::pointer track{ Database::Track::find(LmsApp->getDbSession(), *trackId) };
                if (!track)
                {
                    LOG(ERROR) << "Missing track";
                    return std::nullopt;
                }

                parameters.inputParameters.trackPath = track->getPath();
                parameters.inputParameters.duration = track->getDuration();
            }

            parameters.outputParameters.stripMetadata = true;
            parameters.outputParameters.format = *avFormat;
            parameters.outputParameters.bitrate = *bitrate;
            parameters.outputParameters.offset = std::chrono::seconds{ offset };

            return parameters;
        }
    }

    AudioTranscodingResource:: ~AudioTranscodingResource()
    {
        beingDeleted();
    }

    std::string AudioTranscodingResource::getUrl(Database::TrackId trackId) const
    {
        return url() + "&trackid=" + trackId.toString();
    }

    void AudioTranscodingResource::handleRequest(const Wt::Http::Request& request, Wt::Http::Response& response)
    {
        std::shared_ptr<IResourceHandler> resourceHandler;

        try
        {
            Wt::Http::ResponseContinuation* continuation{ request.continuation() };
            if (!continuation)
            {
                if (const auto& parameters{ readTranscodingParameters(request) })
                    resourceHandler = Av::Transcoding::createResourceHandler(parameters->inputParameters, parameters->outputParameters, false /* estimate content length */);
            }
            else
            {
                resourceHandler = Wt::cpp17::any_cast<std::shared_ptr<IResourceHandler>>(continuation->data());
            }

            if (resourceHandler)
            {
                continuation = resourceHandler->processRequest(request, response);
                if (continuation)
                    continuation->setData(resourceHandler);
            }
        }
        catch (const Av::Exception& e)
        {
            LOG(ERROR) << "Caught Av exception: " << e.what();
        }
    }

} // namespace UserInterface