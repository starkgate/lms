/*
 * Copyright (C) 2014 Emeric Poupon
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
#include "services/database/TrackList.hpp"

#include <cassert>

#include "utils/Logger.hpp"

#include "services/database/Artist.hpp"
#include "services/database/Cluster.hpp"
#include "services/database/Release.hpp"
#include "services/database/Session.hpp"
#include "services/database/User.hpp"
#include "services/database/Track.hpp"
#include "SqlQuery.hpp"
#include "StringViewTraits.hpp"
#include "IdTypeTraits.hpp"
#include "Utils.hpp"

namespace Database
{
    TrackList::TrackList(std::string_view name, TrackListType type, bool isPublic, ObjectPtr<User> user)
        : _name{ name }
        , _type{ type }
        , _isPublic{ isPublic }
        , _creationDateTime{ Utils::normalizeDateTime(Wt::WDateTime::currentDateTime()) }
        , _lastModifiedDateTime{ Utils::normalizeDateTime(Wt::WDateTime::currentDateTime()) }
        , _user{ getDboPtr(user) }
    {
        assert(user);
    }

    TrackList::pointer TrackList::create(Session& session, std::string_view name, TrackListType type, bool isPublic, ObjectPtr<User> user)
    {
        return session.getDboSession().add(std::unique_ptr<TrackList> {new TrackList{ name, type, isPublic, user }});
    }

    std::size_t TrackList::getCount(Session& session)
    {
        session.checkSharedLocked();

        return session.getDboSession().query<int>("SELECT COUNT(*) FROM tracklist");
    }


    TrackList::pointer TrackList::find(Session& session, std::string_view name, TrackListType type, UserId userId)
    {
        session.checkSharedLocked();
        assert(userId.isValid());

        return session.getDboSession().find<TrackList>()
            .where("name = ?").bind(name)
            .where("type = ?").bind(type)
            .where("user_id = ?").bind(userId).resultValue();
    }

    RangeResults<TrackListId> TrackList::find(Session& session, const FindParameters& params)
    {
        session.checkSharedLocked();

        auto query{ session.getDboSession().query<TrackListId>("SELECT DISTINCT t_l.id FROM tracklist t_l") };

        if (params.user.isValid())
            query.where("t_l.user_id = ?").bind(params.user);

        if (params.type)
            query.where("t_l.type = ?").bind(*params.type);

        if (!params.clusters.empty())
        {
            query.join("tracklist_entry t_l_e ON t_l_e.tracklist_id = t_l.id");
            query.join("track t ON t.id = t_l_e.track_id");

            std::ostringstream oss;
            oss << "t.id IN (SELECT DISTINCT t.id FROM track t"
                " INNER JOIN track_cluster t_c ON t_c.track_id = t.id"
                " INNER JOIN cluster c ON c.id = t_c.cluster_id";

            WhereClause clusterClause;
            for (const ClusterId clusterId : params.clusters)
            {
                clusterClause.Or(WhereClause("c.id = ?"));
                query.bind(clusterId);
            }

            oss << " " << clusterClause.get();
            oss << " GROUP BY t.id HAVING COUNT(*) = " << params.clusters.size() << ")";

            query.where(oss.str());
        }

        switch (params.sortMethod)
        {
        case TrackListSortMethod::None:
            break;
        case TrackListSortMethod::Name:
            query.orderBy("t_l.name COLLATE NOCASE");
            break;
        case TrackListSortMethod::LastModifiedDesc:
            query.orderBy("t_l.last_modified_date_time DESC");
            break;
        }

        return Utils::execQuery<TrackListId>(query, params.range);
    }

    TrackList::pointer TrackList::find(Session& session, TrackListId id)
    {
        session.checkSharedLocked();

        return session.getDboSession().find<TrackList>().where("id = ?").bind(id).resultValue();
    }

    bool TrackList::isEmpty() const
    {
        return _entries.empty();
    }

    std::size_t TrackList::getCount() const
    {
        return _entries.size();
    }

    TrackListEntry::pointer TrackList::getEntry(std::size_t pos) const
    {
        TrackListEntry::pointer res;

        auto entries = getEntries(Range{ pos, 1 });
        if (!entries.empty())
            res = entries.front();

        return res;
    }

    std::vector<TrackListEntry::pointer> TrackList::getEntries(std::optional<Range> range) const
    {
        assert(session());

        auto entries{
            session()->find<TrackListEntry>()
            .where("tracklist_id = ?").bind(getId())
            .orderBy("id")
            .limit(range ? static_cast<int>(range->size) + 1 : -1)
            .offset(range ? static_cast<int>(range->offset) : -1)
            .resultList() };

        return std::vector<TrackListEntry::pointer>(entries.begin(), entries.end());
    }

    TrackListEntry::pointer TrackList::getEntryByTrackAndDateTime(ObjectPtr<Track> track, const Wt::WDateTime& dateTime) const
    {
        assert(session());

        return session()->find<TrackListEntry>()
            .where("tracklist_id = ?").bind(getId())
            .where("track_id = ?").bind(track->getId())
            .where("date_time = ?").bind(Utils::normalizeDateTime(dateTime))
            .resultValue();
    }

    std::vector<Cluster::pointer> TrackList::getClusters() const
    {
        assert(session());

        auto res{ session()->query<Wt::Dbo::ptr<Cluster>>("SELECT c from cluster c INNER JOIN track t ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id INNER JOIN tracklist_entry p_e ON p_e.track_id = t.id INNER JOIN tracklist p ON p.id = p_e.tracklist_id")
            .where("p.id = ?").bind(getId())
            .groupBy("c.id")
            .orderBy("COUNT(c.id) DESC")
            .resultList() };

        return std::vector<Cluster::pointer>(res.begin(), res.end());
    }

    std::vector<std::vector<Cluster::pointer>> TrackList::getClusterGroups(const std::vector<ClusterType::pointer>& clusterTypes, std::size_t size) const
    {
        assert(session());
        std::vector<std::vector<Cluster::pointer>> res;

        if (clusterTypes.empty())
            return res;

        auto query{ session()->query<Wt::Dbo::ptr<Cluster>>("SELECT c from cluster c") };

        query.join("track t ON c.id = t_c.cluster_id")
            .join("track_cluster t_c ON t_c.track_id = t.id")
            .join("cluster_type c_type ON c.cluster_type_id = c_type.id")
            .join("tracklist_entry t_l_e ON t_l_e.track_id = t.id")
            .join("tracklist t_l ON t_l.id = t_l_e.tracklist_id")
            .where("t_l.id = ?").bind(getId());

        {
            std::ostringstream oss;
            oss << "c_type.id IN (";
            bool first{ true };
            for (auto clusterType : clusterTypes)
            {
                if (!first)
                    oss << ", ";
                oss << "?";
                query.bind(clusterType->getId());
                first = false;
            }
            oss << ")";
            query.where(oss.str());
        }
        query.groupBy("c.id");
        query.orderBy("COUNT(c.id) DESC");

        auto queryRes{ query.resultList() };

        std::map<ClusterTypeId, std::vector<Cluster::pointer>> clustersByType;
        for (const Wt::Dbo::ptr<Cluster>& cluster : queryRes)
        {
            if (clustersByType[cluster->getType()->getId()].size() < size)
                clustersByType[cluster->getType()->getId()].push_back(cluster);
        }

        for (const auto& [clusterTypeId, clusters] : clustersByType)
            res.push_back(clusters);

        return res;
    }

    bool TrackList::hasTrack(TrackId trackId) const
    {
        assert(session());

        Wt::Dbo::collection<TrackListEntry::pointer> res = session()->query<TrackListEntry::pointer>("SELECT p_e from tracklist_entry p_e INNER JOIN tracklist p ON p_e.tracklist_id = p.id")
            .where("p_e.track_id = ?").bind(trackId)
            .where("p.id = ?").bind(getId());

        return res.size() > 0;
    }

    std::vector<Track::pointer> TrackList::getSimilarTracks(std::optional<std::size_t> offset, std::optional<std::size_t> size) const
    {
        assert(session());

        auto res{ session()->query<Wt::Dbo::ptr<Track>>(
                "SELECT t FROM track t"
                " INNER JOIN track_cluster t_c ON t_c.track_id = t.id"
                    " WHERE "
                        " (t_c.cluster_id IN (SELECT DISTINCT c.id from cluster c INNER JOIN track t ON c.id = t_c.cluster_id INNER JOIN track_cluster t_c ON t_c.track_id = t.id INNER JOIN tracklist_entry p_e ON p_e.track_id = t.id INNER JOIN tracklist p ON p.id = p_e.tracklist_id WHERE p.id = ?)"
                        " AND t.id NOT IN (SELECT tracklist_t.id FROM track tracklist_t INNER JOIN tracklist_entry t_e ON t_e.track_id = tracklist_t.id WHERE t_e.tracklist_id = ?))"
                    )
            .bind(getId())
            .bind(getId())
            .groupBy("t.id")
            .orderBy("COUNT(*) DESC, RANDOM()")
            .limit(size ? static_cast<int>(*size) : -1)
            .offset(offset ? static_cast<int>(*offset) : -1)
            .resultList() };

        return std::vector<Track::pointer>(res.begin(), res.end());
    }

    std::vector<TrackId> TrackList::getTrackIds() const
    {
        assert(session());

        Wt::Dbo::collection<TrackId> res = session()->query<TrackId>("SELECT p_e.track_id from tracklist_entry p_e INNER JOIN tracklist p ON p_e.tracklist_id = p.id")
            .where("p.id = ?").bind(getId());

        return std::vector<TrackId>(res.begin(), res.end());
    }

    std::chrono::milliseconds TrackList::getDuration() const
    {
        assert(session());

        using milli = std::chrono::duration<int, std::milli>;

        Wt::Dbo::Query<milli> query{ session()->query<milli>("SELECT COALESCE(SUM(duration), 0) FROM track t INNER JOIN tracklist_entry p_e ON t.id = p_e.track_id")
                .where("p_e.tracklist_id = ?").bind(getId()) };

        return query.resultValue();
    }

    void TrackList::setLastModifiedDateTime(const Wt::WDateTime& dateTime)
    {
        _lastModifiedDateTime = Utils::normalizeDateTime(dateTime);
    }

    TrackListEntry::TrackListEntry(ObjectPtr<Track> track, ObjectPtr<TrackList> tracklist, const Wt::WDateTime& dateTime)
        : _dateTime{ Utils::normalizeDateTime(dateTime) }
        , _track{ getDboPtr(track) }
        , _tracklist{ getDboPtr(tracklist) }
    {
        assert(track);
        assert(tracklist);
    }

    TrackListEntry::pointer TrackListEntry::create(Session& session, ObjectPtr<Track> track, ObjectPtr<TrackList> tracklist, const Wt::WDateTime& dateTime)
    {
        return session.getDboSession().add(std::unique_ptr<TrackListEntry> {new TrackListEntry{ track, tracklist, dateTime }});
    }

    void TrackListEntry::onPostCreated()
    {
        _tracklist.modify()->setLastModifiedDateTime(Utils::normalizeDateTime(Wt::WDateTime::currentDateTime()));
    }

    void TrackListEntry::onPreRemove()
    {
        _tracklist.modify()->setLastModifiedDateTime(Utils::normalizeDateTime(Wt::WDateTime::currentDateTime()));
    }

    TrackListEntry::pointer TrackListEntry::getById(Session& session, TrackListEntryId id)
    {
        session.checkSharedLocked();

        return session.getDboSession().find<TrackListEntry>().where("id = ?").bind(id).resultValue();
    }

} // namespace Database
