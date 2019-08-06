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

#pragma once

#include <boost/optional.hpp>

#include <Wt/WContainerWidget.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WTemplate.h>

#include "database/Types.hpp"

namespace UserInterface {

class Filters;

class Releases : public Wt::WTemplate
{
	public:
		Releases(Filters* filters);

		Wt::Signal<const std::vector<Database::IdType>&> releasesAdd;
		Wt::Signal<const std::vector<Database::IdType>&> releasesPlay;

	private:
		void refresh();
		void addSome();

		std::vector<Database::IdType> getReleases(boost::optional<std::size_t> offset, boost::optional<std::size_t> limit, bool& moreResults) const;
		std::vector<Database::IdType> getReleases() const;

		Filters* _filters;
		Wt::WPushButton* _showMore;
		Wt::WLineEdit* _search;
		Wt::WContainerWidget* _container;
};

} // namespace UserInterface

