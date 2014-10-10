/*
 * Copyright (C) 2013 Emeric Poupon
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

#include "logger/Logger.hpp"

#include <boost/foreach.hpp>
#include <boost/bind.hpp>

#include "ServiceManager.hpp"

namespace Service {

ServiceManager&
ServiceManager::instance()
{
	static ServiceManager instance;
	return instance;
}

ServiceManager::ServiceManager()
: _signalSet(_ioService)
{
	_signalSet.add(SIGINT);
	_signalSet.add(SIGTERM);
#if defined(SIGQUIT)
	_signalSet.add(SIGQUIT);
#endif // defined(SIGQUIT)

	_signalSet.add(SIGHUP);

	// Excplicitely ignore SIGCHLD to avoid zombies
	// when avconv child processes are being killed
	if (::signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		throw std::runtime_error("ServiceManager::ServiceManager, signal failed!");
}

ServiceManager::~ServiceManager()
{
	LMS_LOG(MOD_SERVICE, SEV_NOTICE) << "Stopping services...";
	stopServices();
}

void
ServiceManager::run()
{

	asyncWaitSignals();

	LMS_LOG(MOD_SERVICE, SEV_DEBUG) << "ServiceManager: waiting for events...";
	try {
		// Wait for events
		_ioService.run();
	}
	catch( std::exception& e )
	{
		LMS_LOG(MOD_SERVICE, SEV_ERROR) << "ServiceManager: exception in ioService::run: " << e.what();
	}

	// Stopping services
	stopServices();

	LMS_LOG(MOD_SERVICE, SEV_DEBUG) << "ServiceManager: run complete !";
}

void
ServiceManager::asyncWaitSignals(void)
{
	_signalSet.async_wait(boost::bind(&ServiceManager::handleSignal,
				this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::signal_number));
}

void
ServiceManager::startService(Service::pointer service)
{
	_services.insert(service);
	service->start();
}

void
ServiceManager::stopService(Service::pointer service)
{
	_services.erase(service);
	service->stop();
}

void
ServiceManager::stopServices(void)
{
	BOOST_FOREACH(Service::pointer service, _services)
		service->stop();
}


void
ServiceManager::restartServices(void)
{
	LMS_LOG(MOD_SERVICE, SEV_NOTICE) << "Restarting services...";
	BOOST_FOREACH(Service::pointer service, _services)
		service->restart();
}

void
ServiceManager::handleSignal(boost::system::error_code /*ec*/, int signo)
{
	LMS_LOG(MOD_SERVICE, SEV_INFO) << "Received signal " << signo;

	switch (signo)
	{
		case SIGINT:
		case SIGTERM:
		case SIGQUIT:
			stopServices();

			// Do not listen for signals, this will make the ioservice.run return
			break;
		case SIGHUP:
			restartServices();

			asyncWaitSignals();
			break;
		default:
			LMS_LOG(MOD_SERVICE, SEV_NOTICE) << "Unhandled signal " << signo;
	}
}

} // namespace Service
