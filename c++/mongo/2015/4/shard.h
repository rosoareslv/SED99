/**
 *    Copyright (C) 2008-2015 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <boost/shared_ptr.hpp>

#include "mongo/client/dbclientinterface.h"

namespace mongo {

    class BSONObj;
    class ShardStatus;

    /*
     * A "shard" one partition of the overall database (and a replica set typically).
     */

    class Shard {
    public:
        Shard();

        Shard(const std::string& name,
              const std::string& addr,
              long long maxSizeMB,
              bool isDraining);

        Shard(const std::string& name,
              const ConnectionString& connStr,
              long long maxSizeMB,
              bool isDraining);

        Shard( const std::string& ident ) {
            reset( ident );
        }

        /**
         * Returns a Shard corresponding to 'ident', which can
         * either be a shard name or a connection string.
         * Assumes that a corresponding shard with name 'ident' already exists.
         */
        static Shard make( const std::string& ident ) {
            Shard s;
            s.reset( ident );
            return s;
        }

        /**
         * Returns a Shard corresponding to 'shardName' if such a shard
         * exists.
         * If not, it returns Shard::EMPTY
         */
        static Shard findIfExists( const std::string& shardName );

        /**
         * @param ident either name or address
         */
        void reset( const std::string& ident );

        const ConnectionString& getAddress() const { return _cs; }
        const std::string& getName() const { return _name; }
        const std::string& getConnString() const { return _addr; }

        long long getMaxSizeMB() const {
            return _maxSizeMB;
        }

        bool isDraining() const {
            return _isDraining;
        }

        std::string toString() const {
            return _name + ":" + _addr;
        }

        friend std::ostream& operator << (std::ostream& out, const Shard& s) {
            return (out << s.toString());
        }

        bool operator==( const Shard& s ) const {
            if ( _name != s._name )
                return false;
            return _cs.sameLogicalEndpoint( s._cs );
        }

        bool operator!=( const Shard& s ) const {
            return ! ( *this == s );
        }

        bool operator==( const std::string& s ) const {
            return _name == s || _addr == s;
        }

        bool operator!=( const std::string& s ) const {
            return _name != s && _addr != s;
        }

        bool operator<(const Shard& o) const {
            return _name < o._name;
        }

        bool ok() const { return _addr.size() > 0; }

        // Set internal to true to run the command with internal authentication privileges.
        BSONObj runCommand( const std::string& db , const std::string& simple ) const {
            return runCommand( db , BSON( simple << 1 ) );
        }
        BSONObj runCommand( const std::string& db , const BSONObj& cmd ) const ;

        /**
         * Runs a command on this shard. This method signature matches that of
         * the connection on which this method relies upon.
         */
        bool runCommand(const std::string& db,
                        const BSONObj& cmd,
                        BSONObj& res) const;

        bool runCommand(const std::string& db,
                        const std::string& simple,
                        BSONObj& res) const {
            return runCommand(db, BSON(simple << 1), res);
        }

        /**
         * Returns the version string from the shard based from the serverStatus command result.
         */
        static std::string getShardMongoVersion(const std::string& shardHost);

        /**
         * Returns the total data size in bytes the shard is currently using.
         */
        static long long getShardDataSizeBytes(const std::string& shardHost);

        /**
         * Returns metadata and stats for this shard.
         */
        ShardStatus getStatus() const ;

        /**
         * mostly for replica set
         * retursn true if node is the shard 
         * of if the replica set contains node
         */
        bool containsNode( const std::string& node ) const;

        static void getAllShards( std::vector<Shard>& all );
        static void printShardInfo( std::ostream& out );
        static Shard lookupRSName( const std::string& name);
        
        /**
         * @parm current - shard where the chunk/database currently lives in
         * @return the currently emptiest shard, if best then current, or EMPTY
         */
        static Shard pick( const Shard& current = EMPTY );

        static void reloadShardInfo();

        static void removeShard( const std::string& name );

        static bool isAShardNode( const std::string& ident );

        static Shard EMPTY;
        
        static void installShard(const std::string& name, const Shard& shard);

    private:

        void _setAddr( const std::string& addr );

        std::string    _name;
        std::string    _addr;
        ConnectionString _cs;
        long long _maxSizeMB;    // in MBytes, 0 is unlimited
        bool      _isDraining; // shard is currently being removed
    };

    typedef boost::shared_ptr<Shard> ShardPtr;

    class ShardStatus {
    public:

        ShardStatus(const Shard& shard, long long dataSizeBytes, const std::string& version);

        friend std::ostream& operator << (std::ostream& out, const ShardStatus& s) {
            out << s.toString();
            return out;
        }

        std::string toString() const {
            std::stringstream ss;
            ss << "shard: " << _shard
               << " dataSizeBytes: " << _dataSizeBytes
               << " version: " << _mongoVersion;
            return ss.str();
        }

        bool operator<( const ShardStatus& other ) const {
            return _dataSizeBytes < other._dataSizeBytes;
        }

        Shard shard() const {
            return _shard;
        }

        long long dataSizeBytes() const {
            return _dataSizeBytes;
        }

        std::string mongoVersion() const {
            return _mongoVersion;
        }

    private:
        Shard _shard;
        long long _dataSizeBytes;
        std::string _mongoVersion;
    };

}
