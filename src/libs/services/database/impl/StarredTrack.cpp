/*
 * Copyright (C) 2021 Emeric Poupon
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

#include "services/database/StarredTrack.hpp"

#include <Wt/Dbo/WtSqlTraits.h>

#include "services/database/Track.hpp"
#include "services/database/Session.hpp"
#include "services/database/User.hpp"
#include "IdTypeTraits.hpp"
#include "Utils.hpp"

namespace Database
{
    StarredTrack::StarredTrack(ObjectPtr<Track> track, ObjectPtr<User> user, FeedbackBackend backend)
        : _backend{ backend }
        , _track{ getDboPtr(track) }
        , _user{ getDboPtr(user) }
    {
    }

    StarredTrack::pointer StarredTrack::create(Session& session, ObjectPtr<Track> track, ObjectPtr<User> user, FeedbackBackend backend)
    {
        return session.getDboSession().add(std::unique_ptr<StarredTrack> {new StarredTrack{ track, user, backend }});
    }

    std::size_t StarredTrack::getCount(Session& session)
    {
        session.checkSharedLocked();
        return session.getDboSession().query<int>("SELECT COUNT(*) FROM starred_track");
    }

    StarredTrack::pointer StarredTrack::find(Session& session, StarredTrackId id)
    {
        session.checkSharedLocked();
        return session.getDboSession().find<StarredTrack>().where("id = ?").bind(id).resultValue();
    }

    StarredTrack::pointer StarredTrack::find(Session& session, TrackId trackId, UserId userId, FeedbackBackend backend)
    {
        session.checkSharedLocked();
        return session.getDboSession().find<StarredTrack>()
            .where("track_id = ?").bind(trackId)
            .where("user_id = ?").bind(userId)
            .where("backend = ?").bind(backend)
            .resultValue();
    }

    RangeResults<StarredTrackId> StarredTrack::find(Session& session, const FindParameters& params)
    {
        session.checkSharedLocked();

        auto query{ session.getDboSession().query<StarredTrackId>("SELECT DISTINCT s_t.id FROM starred_track s_t") };

        if (params.backend)
            query.where("s_t.backend = ?").bind(*params.backend);
        if (params.syncState)
            query.where("s_t.sync_state = ?").bind(*params.syncState);
        if (params.user.isValid())
            query.where("s_t.user_id = ?").bind(params.user);

        return Utils::execQuery<StarredTrackId>(query, params.range);
    }

    void StarredTrack::setDateTime(const Wt::WDateTime& dateTime)
    {
        _dateTime = Utils::normalizeDateTime(dateTime);
    }
}
