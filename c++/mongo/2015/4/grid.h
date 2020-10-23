/**
 *    Copyright (C) 2010-2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>

#include "mongo/stdx/memory.h"

namespace mongo {

    class BSONObj;
    class CatalogCache;
    class CatalogManager;
    class DBConfig;
    class SettingsType;
    template<typename T> class StatusWith;


    /**
     * Holds the global sharding context. Single instance exists for a running server. Exists on
     * both MongoD and MongoS.
     */
    class Grid {
    public:
        Grid();

        /**
         * Called at startup time so the catalog manager can be set. Should be called only once
         * for the lifetime of the grid object. Takes ownership of the passed in pointer.
         */
        void setCatalogManager(std::unique_ptr<CatalogManager> catalogManager);

        /**
         * Implicitly creates the specified database as non-sharded.
         */
        StatusWith<boost::shared_ptr<DBConfig>> implicitCreateDb(const std::string& dbName);

        /**
         * @return true if shards and config servers are allowed to use 'localhost' in address
         */
        bool allowLocalHost() const;

        /**
         * @param whether to allow shards and config servers to use 'localhost' in address
         */
        void setAllowLocalHost( bool allow );

        /**
         * Returns true if the balancer should be running. Caller is responsible
         * for making sure settings has the balancer key.
         */
        bool shouldBalance(const SettingsType& balancerSettings) const;

        /**
         * Returns true if the config server settings indicate that the balancer should be active.
         */
        bool getConfigShouldBalance() const;

        CatalogManager* catalogManager() const { return _catalogManager.get(); }
        CatalogCache* catalogCache() const { return _catalogCache.get(); }

        // exposed methods below are for testing only

        /**
         * @param balancerDoc bson that may contain a window of time for the balancer to work
         *        format { ... , activeWindow: { start: "8:30" , stop: "19:00" } , ... }
         * @return true if there is no window of time specified for the balancer or it we're currently in it
         */
        static bool _inBalancingWindow(const BSONObj& balancerDoc, const boost::posix_time::ptime& now);

    private:
        std::unique_ptr<CatalogManager> _catalogManager;
        std::unique_ptr<CatalogCache> _catalogCache;

        // can 'localhost' be used in shard addresses?
        bool _allowLocalShard;
    };

    extern Grid grid;

} // namespace mongo
