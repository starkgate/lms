/*
 * Copyright (C) 2019 Emeric Poupon
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

#include <list>

#include "Common.hpp"

using namespace Database;

TEST_F(DatabaseFixture, SingleTrackList)
{
    ScopedUser user{ session, "MyUser" };
    {
        auto transaction{ session.createSharedTransaction() };
        EXPECT_EQ(TrackList::getCount(session), 0);
    }

    ScopedTrackList trackList{ session, "MytrackList", TrackListType::Playlist, false, user.lockAndGet() };

    {
        auto transaction{ session.createSharedTransaction() };
        EXPECT_EQ(TrackList::getCount(session), 1);
    }
}

TEST_F(DatabaseFixture, SingleTrackListSingleTrack)
{
    ScopedUser user{ session, "MyUser" };
    ScopedTrackList trackList1{ session, "MyTrackList1", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrackList trackList2{ session, "MyTrackList2", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrack track{ session, "MyTrack" };

    {
        auto transaction{ session.createSharedTransaction() };

        auto tracks{ Track::findIds(session, Track::FindParameters {}.setTrackList(trackList1.getId())) };
        EXPECT_EQ(tracks.results.size(), 0);

        tracks = Track::findIds(session, Track::FindParameters{}.setTrackList(trackList2.getId()));
        EXPECT_EQ(tracks.results.size(), 0);
    }

    {
        auto transaction{ session.createUniqueTransaction() };

        session.create<TrackListEntry>(track.get(), trackList1.get());
    }

    {
        auto transaction{ session.createSharedTransaction() };

        auto tracks{ Track::findIds(session, Track::FindParameters {}.setTrackList(trackList1.getId())) };
        ASSERT_EQ(tracks.results.size(), 1);
        EXPECT_EQ(tracks.results.front(), track.getId());

        tracks = Track::findIds(session, Track::FindParameters{}.setTrackList(trackList2.getId()));
        EXPECT_EQ(tracks.results.size(), 0);
    }
}

TEST_F(DatabaseFixture, TrackList_SortMethod)
{
    ScopedUser user{ session, "MyUser" };
    ScopedTrackList trackList2{ session, "MyTrackList2", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrackList trackList1{ session, "MyTrackList1", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrack track{ session, "MyTrack" };

    {
        auto transaction{ session.createSharedTransaction() };

        const auto trackLists{ TrackList::find(session, TrackList::FindParameters {}.setSortMethod(TrackListSortMethod::Name)) };
        ASSERT_EQ(trackLists.results.size(), 2);
        EXPECT_EQ(trackLists.results[0], trackList1.getId());
        EXPECT_EQ(trackLists.results[1], trackList2.getId());
    }

    {
        auto transaction{ session.createUniqueTransaction() };

        trackList1.get().modify()->setLastModifiedDateTime(Wt::WDateTime{ Wt::WDate {1900,1,1} });
        trackList2.get().modify()->setLastModifiedDateTime(Wt::WDateTime{ Wt::WDate {1900,1,2} });
    }

    {
        auto transaction{ session.createSharedTransaction() };

        const auto trackLists{ TrackList::find(session, TrackList::FindParameters {}.setSortMethod(TrackListSortMethod::LastModifiedDesc)) };
        ASSERT_EQ(trackLists.results.size(), 2);
        EXPECT_EQ(trackLists.results[0], trackList2.getId());
        EXPECT_EQ(trackLists.results[1], trackList1.getId());
    }

    {
        auto transaction{ session.createUniqueTransaction() };

        trackList1.get().modify()->setLastModifiedDateTime(Wt::WDateTime{ Wt::WDate {1900,1,2} });
        trackList2.get().modify()->setLastModifiedDateTime(Wt::WDateTime{ Wt::WDate {1900,1,1} });
    }

    {
        auto transaction{ session.createSharedTransaction() };

        const auto trackLists{ TrackList::find(session, TrackList::FindParameters {}.setSortMethod(TrackListSortMethod::LastModifiedDesc)) };
        ASSERT_EQ(trackLists.results.size(), 2);
        EXPECT_EQ(trackLists.results[0], trackList1.getId());
        EXPECT_EQ(trackLists.results[1], trackList2.getId());
    }
}

TEST_F(DatabaseFixture, SingleTrackListMultipleTrack)
{
    ScopedUser user{ session, "MyUser" };
    ScopedTrackList trackList{ session, "MytrackList", TrackListType::Playlist, false, user.lockAndGet() };
    std::list<ScopedTrack> tracks;

    for (std::size_t i{}; i < 10; ++i)
    {
        tracks.emplace_back(session, "MyTrack" + std::to_string(i));

        auto transaction{ session.createUniqueTransaction() };
        session.create<TrackListEntry>(tracks.back().get(), trackList.get());
    }

    {
        auto transaction{ session.createSharedTransaction() };

        ASSERT_EQ(trackList->getCount(), tracks.size());
        const auto trackIds{ trackList->getTrackIds() };
        ASSERT_EQ(trackIds.size(), tracks.size());

        // Same order
        std::size_t i{};
        for (const ScopedTrack& track : tracks)
            EXPECT_EQ(track.getId(), trackIds[i++]);
    }
}

TEST_F(DatabaseFixture, SingleTrackListSingleTrackWithCluster)
{
    ScopedUser user{ session, "MyUser" };
    ScopedTrackList trackList1{ session, "MyTrackList1", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrackList trackList2{ session, "MyTrackList2", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedClusterType clusterType{ session, "MyClusterType" };
    ScopedCluster cluster{ session, clusterType.lockAndGet(), "MyCluster" };
    ScopedTrack track{ session, "MyTrack" };

    {
        auto transaction{ session.createSharedTransaction() };

        auto trackLists{ TrackList::find(session, TrackList::FindParameters {}.setClusters({cluster.getId()})) };
        EXPECT_EQ(trackLists.results.size(), 0);
    }

    {
        auto transaction{ session.createUniqueTransaction() };

        session.create<TrackListEntry>(track.get(), trackList1.get());
        cluster.get().modify()->addTrack(track.get());
    }

    {
        auto transaction{ session.createSharedTransaction() };

        auto trackLists{ TrackList::find(session, TrackList::FindParameters {}.setClusters({cluster.getId()})) };
        ASSERT_EQ(trackLists.results.size(), 1);
        EXPECT_EQ(trackLists.results.front(), trackList1.getId());
    }
}

TEST_F(DatabaseFixture, SingleTrackList_getEntries)
{
    ScopedUser user{ session, "MyUser" };
    ScopedTrackList trackList{ session, "MyTrackList", TrackListType::Playlist, false, user.lockAndGet() };
    ScopedTrack track1{ session, "MyTrack" };
    ScopedTrack track2{ session, "MyTrack" };

    {
        auto transaction{ session.createUniqueTransaction() };
        session.create<TrackListEntry>(track1.get(), trackList.get());
    }

    {
        auto transaction{ session.createSharedTransaction() };
        auto entries{ trackList.get()->getEntries() };
        ASSERT_EQ(entries.size(), 1);
        EXPECT_EQ(entries.front()->getTrack()->getId(), track1.getId());
    }

    {
        auto transaction{ session.createUniqueTransaction() };
        session.create<TrackListEntry>(track2.get(), trackList.get());
    }

    {
        auto transaction{ session.createSharedTransaction() };
        auto entries{ trackList.get()->getEntries() };
        ASSERT_EQ(entries.size(), 2);
        EXPECT_EQ(entries[0]->getTrack()->getId(), track1.getId());
        EXPECT_EQ(entries[1]->getTrack()->getId(), track2.getId());
    }

    {
        auto transaction{ session.createSharedTransaction() };
        auto entries{ trackList.get()->getEntries(Range {1, 1}) };
        ASSERT_EQ(entries.size(), 1);
        EXPECT_EQ(entries[0]->getTrack()->getId(), track2.getId());
    }
}
