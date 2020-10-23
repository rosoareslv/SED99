// s/commands_public.cpp

/**
*    Copyright (C) 2008 10gen Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include <tuple>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "mongo/client/connpool.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_manager_global.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/copydb.h"
#include "mongo/db/commands/find_and_modify.h"
#include "mongo/db/commands/mr.h"
#include "mongo/db/commands/rename_collection.h"
#include "mongo/db/lasterror.h"
#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/db/query/lite_parsed_query.h"
#include "mongo/s/catalog/catalog_cache.h"
#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/dist_lock_manager.h"
#include "mongo/s/chunk_manager.h"
#include "mongo/s/cluster_explain.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/commands/cluster_commands_common.h"
#include "mongo/s/commands/run_on_all_shards_cmd.h"
#include "mongo/s/config.h"
#include "mongo/s/cursors.h"
#include "mongo/s/grid.h"
#include "mongo/s/stale_exception.h"
#include "mongo/s/strategy.h"
#include "mongo/s/version_manager.h"
#include "mongo/scripting/engine.h"
#include "mongo/util/log.h"
#include "mongo/util/net/message.h"
#include "mongo/util/timer.h"

namespace mongo {

    using boost::intrusive_ptr;
    using boost::scoped_ptr;
    using boost::shared_ptr;
    using std::endl;
    using std::list;
    using std::make_pair;
    using std::map;
    using std::multimap;
    using std::set;
    using std::string;
    using std::stringstream;
    using std::vector;

namespace {

    bool appendEmptyResultSet(BSONObjBuilder& result, Status status, const std::string& ns) {
        invariant(!status.isOK());

        if (status == ErrorCodes::DatabaseNotFound) {
            result << "result" << BSONArray()
                   << "cursor" << BSON("id" << 0LL <<
                                       "ns" << ns <<
                                       "firstBatch" << BSONArray());
            return true;
        }

        return Command::appendCommandStatus(result, status);
    }

}

    namespace dbgrid_pub_cmds {

        namespace {

            /**
             * Utility function to parse a cursor command response and save the cursor in the
             * CursorCache "refs" container.  Returns Status::OK() if the cursor was successfully
             * saved or no cursor was specified in the command response, and returns an error Status
             * if a parsing error was encountered.
             */
            Status storePossibleCursor(const std::string& server, const BSONObj& cmdResult) {
                if (cmdResult["ok"].trueValue() && cmdResult.hasField("cursor")) {
                    BSONElement cursorIdElt = cmdResult.getFieldDotted("cursor.id");
                    if (cursorIdElt.type() != mongo::NumberLong) {
                        return Status(ErrorCodes::TypeMismatch,
                                      str::stream() << "expected \"cursor.id\" field from shard "
                                                    << "response to have NumberLong type, instead "
                                                    << "got: " << typeName(cursorIdElt.type()));
                    }
                    const long long cursorId = cursorIdElt.Long();
                    if (cursorId != 0) {
                        BSONElement cursorNsElt = cmdResult.getFieldDotted("cursor.ns");
                        if (cursorNsElt.type() != mongo::String) {
                            return Status(ErrorCodes::TypeMismatch,
                                          str::stream() << "expected \"cursor.ns\" field from "
                                                        << "shard response to have String type, "
                                                        << "instead got: "
                                                        << typeName(cursorNsElt.type()));
                        }
                        const std::string cursorNs = cursorNsElt.String();
                        cursorCache.storeRef(server, cursorId, cursorNs);
                    }
                }
                return Status::OK();
            }

        } // namespace

        class PublicGridCommand : public Command {
        public:
            PublicGridCommand( const char* n, const char* oldname=NULL ) : Command( n, false, oldname ) {
            }
            virtual bool slaveOk() const {
                return true;
            }
            virtual bool adminOnly() const {
                return false;
            }

            // Override if passthrough should also send query options
            // Safer as off by default, can slowly enable as we add more tests
            virtual bool passOptions() const { return false; }

            // all grid commands are designed not to lock
            virtual bool isWriteCommandForConfigServer() const { return false; }

        protected:

            bool passthrough( DBConfigPtr conf, const BSONObj& cmdObj , BSONObjBuilder& result ) {
                return _passthrough(conf->name(), conf, cmdObj, 0, result);
            }

            bool adminPassthrough( DBConfigPtr conf, const BSONObj& cmdObj , BSONObjBuilder& result ) {
                return _passthrough("admin", conf, cmdObj, 0, result);
            }

            bool passthrough( DBConfigPtr conf, const BSONObj& cmdObj , int options, BSONObjBuilder& result ) {
                return _passthrough(conf->name(), conf, cmdObj, options, result);
            }

        private:
            bool _passthrough(const string& db,  DBConfigPtr conf, const BSONObj& cmdObj , int options , BSONObjBuilder& result ) {
                ShardConnection conn(conf->getPrimary().getConnString(), "");
                BSONObj res;
                bool ok = conn->runCommand( db , cmdObj , res , passOptions() ? options : 0 );
                if ( ! ok && res["code"].numberInt() == SendStaleConfigCode ) {
                    conn.done();
                    throw RecvStaleConfigException( "command failed because of stale config", res );
                }

                result.appendElements( res );
                conn.done();
                return ok;
            }
        };

        class AllShardsCollectionCommand : public RunOnAllShardsCommand {
        public:
            AllShardsCollectionCommand(const char* n,
                                       const char* oldname = NULL,
                                       bool useShardConn = false):
                                           RunOnAllShardsCommand(n, oldname, useShardConn) {
            }

            virtual void getShards(const string& dbName , BSONObj& cmdObj, set<Shard>& shards) {
                const string fullns = dbName + '.' + cmdObj.firstElement().valuestrsafe();

                auto status = grid.catalogCache()->getDatabase(dbName);
                uassertStatusOK(status.getStatus());

                shared_ptr<DBConfig> conf = status.getValue();

                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    shards.insert(conf->getShard(fullns));
                }
                else {
                    vector<Shard> shardList;
                    Shard::getAllShards(shardList);
                    shards.insert(shardList.begin(), shardList.end());
                }
            }
        };


        class NotAllowedOnShardedCollectionCmd : public PublicGridCommand {
        public:
            NotAllowedOnShardedCollectionCmd( const char * n ) : PublicGridCommand( n ) {}

            virtual bool run(OperationContext* txn,
                             const string& dbName,
                             BSONObj& cmdObj,
                             int options,
                             string& errmsg,
                             BSONObjBuilder& result) {

                const string fullns = parseNs(dbName, cmdObj);

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                if (!conf->isSharded(fullns)) {
                    return passthrough( conf , cmdObj , options, result );
                }

                return appendCommandStatus(result,
                                           Status(ErrorCodes::IllegalOperation,
                                                  str::stream() << "can't do command: " << name
                                                                << " on sharded collection"));
            }

        };

        // ----

        class DropIndexesCmd : public AllShardsCollectionCommand {
        public:
            DropIndexesCmd() :  AllShardsCollectionCommand("dropIndexes", "deleteIndexes") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::dropIndex);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
        } dropIndexesCmd;

        class CreateIndexesCmd : public AllShardsCollectionCommand {
        public:
            CreateIndexesCmd():
                AllShardsCollectionCommand("createIndexes",
                                           NULL, /* oldName */
                                           true /* use ShardConnection */) {
                // createIndexes command should use ShardConnection so the getLastError would
                // be able to properly enforce the write concern (via the saveGLEStats callback).
            }

            /**
             * the createIndexes command doesn't require the 'ns' field to be populated
             * so we make sure its here as its needed for the system.indexes insert
             */
            BSONObj fixSpec( const NamespaceString& ns, const BSONObj& original ) const {
                if ( original["ns"].type() == String )
                    return original;
                BSONObjBuilder bb;
                bb.appendElements( original );
                bb.append( "ns", ns.toString() );
                return bb.obj();
            }

            /**
             * @return equivalent of gle
             */
            BSONObj createIndexLegacy( const string& server,
                                       const NamespaceString& nss,
                                       const BSONObj& spec ) const {
                try {
                    ScopedDbConnection conn( server );
                    conn->insert( nss.getSystemIndexesCollection(), spec );
                    BSONObj gle = conn->getLastErrorDetailed( nss.db().toString() );
                    conn.done();
                    return gle;
                }
                catch ( DBException& e ) {
                    BSONObjBuilder b;
                    b.append( "errmsg", e.toString() );
                    b.append( "code", e.getCode() );
                    return b.obj();
                }
            }

            virtual BSONObj specialErrorHandler( const string& server,
                                                 const string& dbName,
                                                 const BSONObj& cmdObj,
                                                 const BSONObj& originalResult ) const {
                string errmsg = originalResult["errmsg"];
                if ( errmsg.find( "no such cmd" ) == string::npos ) {
                    // cannot use codes as 2.4 didn't have a code for this
                    return originalResult;
                }

                // we need to down convert

                NamespaceString nss( dbName, cmdObj["createIndexes"].String() );

                if ( cmdObj["indexes"].type() != Array )
                    return originalResult;

                BSONObjBuilder newResult;
                newResult.append( "note", "downgraded" );
                newResult.append( "sentTo", server );

                BSONArrayBuilder individualResults;

                bool ok = true;

                BSONObjIterator indexIterator( cmdObj["indexes"].Obj() );
                while ( indexIterator.more() ) {
                    BSONObj spec = indexIterator.next().Obj();
                    spec = fixSpec( nss, spec );

                    BSONObj gle = createIndexLegacy( server, nss, spec );

                    individualResults.append( BSON( "spec" << spec <<
                                                    "gle" << gle ) );

                    BSONElement e = gle["errmsg"];
                    if ( e.type() == String && e.String().size() > 0 ) {
                        ok = false;
                        newResult.appendAs( e, "errmsg" );
                        break;
                    }

                    e = gle["err"];
                    if ( e.type() == String && e.String().size() > 0 ) {
                        ok = false;
                        newResult.appendAs( e, "errmsg" );
                        break;
                    }

                }

                newResult.append( "eachIndex", individualResults.arr() );

                newResult.append( "ok", ok ? 1 : 0 );
                return newResult.obj();
            }

            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::createIndex);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

        } createIndexesCmd;

        class ReIndexCmd : public AllShardsCollectionCommand {
        public:
            ReIndexCmd() :  AllShardsCollectionCommand("reIndex") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::reIndex);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
        } reIndexCmd;

        class CollectionModCmd : public AllShardsCollectionCommand {
        public:
            CollectionModCmd() :  AllShardsCollectionCommand("collMod") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::collMod);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
        } collectionModCmd;

        class ProfileCmd : public PublicGridCommand {
        public:
            ProfileCmd() :  PublicGridCommand("profile") {}
            virtual bool run(OperationContext* txn,
                             const string& dbName,
                             BSONObj& cmdObj,
                             int options,
                             string& errmsg,
                             BSONObjBuilder& result) {
                errmsg = "profile currently not supported via mongos";
                return false;
            }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::enableProfiler);
                out->push_back(Privilege(ResourcePattern::forDatabaseName(dbname), actions));
            }
        } profileCmd;
        

        class ValidateCmd : public AllShardsCollectionCommand {
        public:
            ValidateCmd() :  AllShardsCollectionCommand("validate") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::validate);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
            virtual void aggregateResults(const vector<ShardAndReply>& results,
                                          BSONObjBuilder& output) {

                for (vector<ShardAndReply>::const_iterator it(results.begin()), end(results.end());
                     it!=end; it++) {
                    const BSONObj& result = std::get<1>(*it);
                    const BSONElement valid = result["valid"];
                    if (!valid.eoo()){
                        if (!valid.trueValue()) {
                            output.appendBool("valid", false);
                            return;
                        }
                    }
                    else {
                        // Support pre-1.9.0 output with everything in a big string
                        const char* s = result["result"].valuestrsafe();
                        if (strstr(s, "exception") ||  strstr(s, "corrupt")){
                            output.appendBool("valid", false);
                            return;
                        }
                    }
                }

                output.appendBool("valid", true);
            }
        } validateCmd;

        class RepairDatabaseCmd : public RunOnAllShardsCommand {
        public:
            RepairDatabaseCmd() :  RunOnAllShardsCommand("repairDatabase") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::repairDatabase);
                out->push_back(Privilege(ResourcePattern::forDatabaseName(dbname), actions));
            }
        } repairDatabaseCmd;

        class DBStatsCmd : public RunOnAllShardsCommand {
        public:
            DBStatsCmd() :  RunOnAllShardsCommand("dbStats", "dbstats") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::dbStats);
                out->push_back(Privilege(ResourcePattern::forDatabaseName(dbname), actions));
            }

            virtual void aggregateResults(const vector<ShardAndReply>& results,
                                          BSONObjBuilder& output) {
                long long objects = 0;
                long long unscaledDataSize = 0;
                long long dataSize = 0;
                long long storageSize = 0;
                long long numExtents = 0;
                long long indexes = 0;
                long long indexSize = 0;
                long long fileSize = 0;

                long long freeListNum = 0;
                long long freeListSize = 0;

                for (vector<ShardAndReply>::const_iterator it(results.begin()), end(results.end());
                     it != end; ++it) {
                    const BSONObj& b = std::get<1>(*it);
                    objects     += b["objects"].numberLong();
                    unscaledDataSize    += b["avgObjSize"].numberLong() * b["objects"].numberLong();
                    dataSize    += b["dataSize"].numberLong();
                    storageSize += b["storageSize"].numberLong();
                    numExtents  += b["numExtents"].numberLong();
                    indexes     += b["indexes"].numberLong();
                    indexSize   += b["indexSize"].numberLong();
                    fileSize    += b["fileSize"].numberLong();

                    if ( b["extentFreeList"].isABSONObj() ) {
                        freeListNum += b["extentFreeList"].Obj()["num"].numberLong();
                        freeListSize += b["extentFreeList"].Obj()["totalSize"].numberLong();
                    }
                }

                //result.appendNumber( "collections" , ncollections ); //TODO: need to find a good way to get this
                output.appendNumber( "objects" , objects );
                /* avgObjSize on mongod is not scaled based on the argument to db.stats(), so we use
                 * unscaledDataSize here for consistency.  See SERVER-7347. */
                output.append      ( "avgObjSize" , objects == 0 ? 0 : double(unscaledDataSize) /
                                                                       double(objects) );
                output.appendNumber( "dataSize" , dataSize );
                output.appendNumber( "storageSize" , storageSize);
                output.appendNumber( "numExtents" , numExtents );
                output.appendNumber( "indexes" , indexes );
                output.appendNumber( "indexSize" , indexSize );
                output.appendNumber( "fileSize" , fileSize );

                {
                    BSONObjBuilder extentFreeList( output.subobjStart( "extentFreeList" ) );
                    extentFreeList.appendNumber( "num", freeListNum );
                    extentFreeList.appendNumber( "totalSize", freeListSize );
                    extentFreeList.done();
                }
            }
        } DBStatsCmdObj;

        class CreateCmd : public PublicGridCommand {
        public:
            CreateCmd() : PublicGridCommand( "create" ) {}
            virtual Status checkAuthForCommand(ClientBasic* client,
                                               const std::string& dbname,
                                               const BSONObj& cmdObj) {
                AuthorizationSession* authzSession = AuthorizationSession::get(client);
                if (cmdObj["capped"].trueValue()) {
                    if (!authzSession->isAuthorizedForActionsOnResource(
                            parseResourcePattern(dbname, cmdObj), ActionType::convertToCapped)) {
                        return Status(ErrorCodes::Unauthorized, "unauthorized");
                    }
                }

                // ActionType::createCollection or ActionType::insert are both acceptable
                if (authzSession->isAuthorizedForActionsOnResource(
                        parseResourcePattern(dbname, cmdObj), ActionType::createCollection) ||
                    authzSession->isAuthorizedForActionsOnResource(
                        parseResourcePattern(dbname, cmdObj), ActionType::insert)) {
                    return Status::OK();
                }

                return Status(ErrorCodes::Unauthorized, "unauthorized");
            }
            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {

                auto status = grid.implicitCreateDb(dbName);
                if (!status.isOK()) {
                    return appendCommandStatus(result, status.getStatus());
                }

                shared_ptr<DBConfig> conf = status.getValue();

                return passthrough(conf, cmdObj, result);
            }

        } createCmd;

        class DropCmd : public PublicGridCommand {
        public:
            DropCmd() : PublicGridCommand( "drop" ) {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::dropCollection);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int options,
                     string& errmsg,
                     BSONObjBuilder& result) {

                auto status = grid.catalogCache()->getDatabase(dbName);
                if (!status.isOK()) {
                    if (status == ErrorCodes::DatabaseNotFound) {
                        return true;
                    }

                    return appendCommandStatus(result, status.getStatus());
                }

                shared_ptr<DBConfig> conf = status.getValue();

                const string fullns = dbName + "." + cmdObj.firstElement().valuestrsafe();
                log() << "DROP: " << fullns;

                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    log() << "\tdrop going to do passthrough" << endl;
                    return passthrough( conf , cmdObj , result );
                }

                //
                // TODO: There will be problems if we simultaneously shard and drop a collection
                //

                ChunkManagerPtr cm;
                ShardPtr primary;
                conf->getChunkManagerOrPrimary( fullns, cm, primary );

                if( ! cm ) {
                    log() << "\tdrop going to do passthrough after re-check" << endl;
                    return passthrough( conf , cmdObj , result );
                }

                uassertStatusOK(grid.catalogManager()->dropCollection(fullns));

                if( ! conf->removeSharding( fullns ) ){
                    warning() << "collection " << fullns
                              << " was reloaded as unsharded before drop completed"
                              << " during single drop" << endl;
                }

                return 1;
            }
        } dropCmd;

        class RenameCollectionCmd : public PublicGridCommand {
        public:
            RenameCollectionCmd() : PublicGridCommand( "renameCollection" ) {}
            virtual Status checkAuthForCommand(ClientBasic* client,
                                               const std::string& dbname,
                                               const BSONObj& cmdObj) {
                return rename_collection::checkAuthForRenameCollectionCommand(client,
                                                                              dbname,
                                                                              cmdObj);
            }
            virtual bool adminOnly() const {
                return true;
            }
            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullnsFrom = cmdObj.firstElement().valuestrsafe();
                const string dbNameFrom = nsToDatabase(fullnsFrom);
                auto confFrom = uassertStatusOK(grid.catalogCache()->getDatabase(dbNameFrom));

                const string fullnsTo = cmdObj["to"].valuestrsafe();
                const string dbNameTo = nsToDatabase(fullnsTo);
                auto confTo = uassertStatusOK(grid.catalogCache()->getDatabase(dbNameTo));

                uassert(13138, "You can't rename a sharded collection", !confFrom->isSharded(fullnsFrom));
                uassert(13139, "You can't rename to a sharded collection", !confTo->isSharded(fullnsTo));

                const Shard& shardTo = confTo->getShard(fullnsTo);
                const Shard& shardFrom = confFrom->getShard(fullnsFrom);

                uassert(13137, "Source and destination collections must be on same shard", shardFrom == shardTo);

                return adminPassthrough( confFrom , cmdObj , result );
            }
        } renameCollectionCmd;

        class CopyDBCmd : public PublicGridCommand {
        public:
            CopyDBCmd() : PublicGridCommand( "copydb" ) {}
            virtual bool adminOnly() const {
                return true;
            }
            virtual Status checkAuthForCommand(ClientBasic* client,
                                               const std::string& dbname,
                                               const BSONObj& cmdObj) {
                return copydb::checkAuthForCopydbCommand(client, dbname, cmdObj);
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {

                const string todb = cmdObj.getStringField("todb");
                uassert(ErrorCodes::EmptyFieldName, "missing todb argument", !todb.empty());
                uassert(ErrorCodes::InvalidNamespace, "invalid todb argument", nsIsDbOnly(todb));

                auto confTo = uassertStatusOK(grid.implicitCreateDb(todb));
                uassert(ErrorCodes::IllegalOperation,
                        "cannot copy to a sharded database",
                        !confTo->isShardingEnabled());

                const string fromhost = cmdObj.getStringField("fromhost");
                if (!fromhost.empty()) {
                    return adminPassthrough( confTo , cmdObj , result );
                }
                else {
                    const string fromdb = cmdObj.getStringField("fromdb");
                    uassert(13399, "need a fromdb argument", !fromdb.empty());

                    shared_ptr<DBConfig> confFrom =
                            uassertStatusOK(grid.catalogCache()->getDatabase(fromdb));

                    uassert(13400, "don't know where source DB is", confFrom);
                    uassert(13401, "cant copy from sharded DB", !confFrom->isShardingEnabled());

                    BSONObjBuilder b;
                    BSONForEach(e, cmdObj) {
                        if (strcmp(e.fieldName(), "fromhost") != 0) {
                            b.append(e);
                        }
                    }

                    b.append("fromhost", confFrom->getPrimary().getConnString());
                    BSONObj fixed = b.obj();

                    return adminPassthrough( confTo , fixed , result );
                }

            }

        } clusterCopyDBCmd;

        class CollectionStats : public PublicGridCommand {
        public:
            CollectionStats() : PublicGridCommand("collStats", "collstats") { }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::collStats);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullns = parseNs(dbName, cmdObj);

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    result.appendBool("sharded", false);
                    result.append( "primary" , conf->getPrimary().getName() );

                    return passthrough( conf , cmdObj , result);
                }

                result.appendBool("sharded", true);

                ChunkManagerPtr cm = conf->getChunkManager( fullns );
                massert( 12594 ,  "how could chunk manager be null!" , cm );

                set<Shard> servers;
                cm->getAllShards(servers);

                BSONObjBuilder shardStats;
                map<string,long long> counts;
                map<string,long long> indexSizes;
                /*
                long long count=0;
                long long size=0;
                long long storageSize=0;
                */
                int nindexes=0;
                bool warnedAboutIndexes = false;
                for ( set<Shard>::iterator i=servers.begin(); i!=servers.end(); i++ ) {
                    BSONObj res;
                    {
                        ScopedDbConnection conn(i->getConnString());
                        if ( ! conn->runCommand( dbName , cmdObj , res ) ) {
                            if ( !res["code"].eoo() ) {
                                result.append( res["code"] );
                            }
                            errmsg = "failed on shard: " + res.toString();
                            return false;
                        }
                        conn.done();
                    }
                    
                    BSONObjIterator j( res );
                    while ( j.more() ) {
                        BSONElement e = j.next();

                        if ( str::equals( e.fieldName() , "ns" ) || 
                             str::equals( e.fieldName() , "ok" ) || 
                             str::equals( e.fieldName() , "avgObjSize" ) ||
                             str::equals( e.fieldName() , "lastExtentSize" ) ||
                             str::equals( e.fieldName() , "paddingFactor" ) ) {
                            continue;
                        }
                        else if ( str::equals( e.fieldName() , "count" ) ||
                                  str::equals( e.fieldName() , "size" ) ||
                                  str::equals( e.fieldName() , "storageSize" ) ||
                                  str::equals( e.fieldName() , "numExtents" ) ||
                                  str::equals( e.fieldName() , "totalIndexSize" ) ) {
                            counts[e.fieldName()] += e.numberLong();
                        }
                        else if ( str::equals( e.fieldName() , "indexSizes" ) ) {
                            BSONObjIterator k( e.Obj() );
                            while ( k.more() ) {
                                BSONElement temp = k.next();
                                indexSizes[temp.fieldName()] += temp.numberLong();
                            }
                        }
                        // no longer used since 2.2
                        else if ( str::equals( e.fieldName() , "flags" ) ) {
                            if ( ! result.hasField( e.fieldName() ) )
                                result.append( e );
                        }
                        // flags broken out in 2.4+
                        else if ( str::equals( e.fieldName() , "systemFlags" ) ) {
                            if ( ! result.hasField( e.fieldName() ) )
                                result.append( e );
                        }
                        else if ( str::equals( e.fieldName() , "userFlags" ) ) {
                            if ( ! result.hasField( e.fieldName() ) )
                                result.append( e );
                        }
                        else if ( str::equals( e.fieldName() , "capped" ) ) {
                            if ( ! result.hasField( e.fieldName() ) )
                                result.append( e );
                        }
                        else if ( str::equals( e.fieldName() , "paddingFactorNote" ) ) {
                            if ( ! result.hasField( e.fieldName() ) )
                                result.append( e );
                        }
                        else if ( str::equals( e.fieldName() , "indexDetails" ) ) {
                            //skip this field in the rollup
                        }
                        else if ( str::equals( e.fieldName() , "wiredTiger" ) ) {
                            //skip this field in the rollup
                        }
                        else if ( str::equals( e.fieldName() , "nindexes" ) ) {
                            int myIndexes = e.numberInt();
                            
                            if ( nindexes == 0 ) {
                                nindexes = myIndexes;
                            }
                            else if ( nindexes == myIndexes ) {
                                // no-op
                            }
                            else {
                                // hopefully this means we're building an index
                                
                                if ( myIndexes > nindexes )
                                    nindexes = myIndexes;
                                
                                if ( ! warnedAboutIndexes ) {
                                    result.append( "warning" , "indexes don't all match - ok if ensureIndex is running" );
                                    warnedAboutIndexes = true;
                                }
                            }
                        }
                        else {
                            warning() << "mongos collstats doesn't know about: " << e.fieldName() << endl;
                        }
                        
                    }
                    shardStats.append(i->getName(), res);
                }

                result.append("ns", fullns);
                
                for ( map<string,long long>::iterator i=counts.begin(); i!=counts.end(); ++i )
                    result.appendNumber( i->first , i->second );
                
                {
                    BSONObjBuilder ib( result.subobjStart( "indexSizes" ) );
                    for ( map<string,long long>::iterator i=indexSizes.begin(); i!=indexSizes.end(); ++i )
                        ib.appendNumber( i->first , i->second );
                    ib.done();
                }

                if ( counts["count"] > 0 )
                    result.append("avgObjSize", (double)counts["size"] / (double)counts["count"] );
                else
                    result.append( "avgObjSize", 0.0 );
                
                result.append("nindexes", nindexes);

                result.append("nchunks", cm->numChunks());
                result.append("shards", shardStats.obj());

                return true;
            }
        } collectionStatsCmd;

        class FindAndModifyCmd : public PublicGridCommand {
        public:
            FindAndModifyCmd() : PublicGridCommand("findAndModify", "findandmodify") { }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                find_and_modify::addPrivilegesRequiredForFindAndModify(this, dbname, cmdObj, out);
            }

            Status explain(OperationContext* txn,
                           const std::string& dbName,
                           const BSONObj& cmdObj,
                           ExplainCommon::Verbosity verbosity,
                           BSONObjBuilder* out) const {
                const string ns = parseNsCollectionRequired(dbName, cmdObj);

                auto status = grid.catalogCache()->getDatabase(dbName);
                uassertStatusOK(status);
                DBConfigPtr conf = status.getValue();

                Shard shard;
                if (!conf->isShardingEnabled() || !conf->isSharded(ns)) {
                    shard = conf->getPrimary();
                }
                else {
                    ChunkManagerPtr chunkMgr = getChunkManager(conf, ns);

                    const BSONObj query = cmdObj.getObjectField("query");
                    StatusWith<BSONObj> status = getShardKey(chunkMgr, ns, query);
                    if (!status.isOK()) {
                        return status.getStatus();
                    }

                    BSONObj shardKey = status.getValue();
                    ChunkPtr chunk = chunkMgr->findIntersectingChunk(shardKey);
                    shard = chunk->getShard();
                }

                BSONObjBuilder explainCmd;
                ClusterExplain::wrapAsExplain(cmdObj, verbosity, &explainCmd);

                // Time how long it takes to run the explain command on the shard.
                Timer timer;

                BSONObjBuilder result;
                bool ok = runCommand(conf, shard, ns, explainCmd.obj(), result);
                long long millisElapsed = timer.millis();

                if (!ok) {
                    BSONObj res = result.obj();
                    return Status(ErrorCodes::OperationFailed, str::stream()
                        << "Explain for findAndModify command failed: " << res);
                }

                Strategy::CommandResult cmdResult;
                cmdResult.shardTarget = shard;
                cmdResult.target = shard.getAddress();
                cmdResult.result = result.obj();

                vector<Strategy::CommandResult> shardResults;
                shardResults.push_back(cmdResult);

                return ClusterExplain::buildExplainResult(shardResults,
                                                          ClusterExplain::kSingleShard,
                                                          millisElapsed,
                                                          out);
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {

                const string ns = parseNsCollectionRequired(dbName, cmdObj);

                // findAndModify should only be creating database if upsert is true, but this
                // would require that the parsing be pulled into this function.
                auto conf = uassertStatusOK(grid.implicitCreateDb(dbName));
                if (!conf->isShardingEnabled() || !conf->isSharded(ns)) {
                    Shard shard = conf->getPrimary();
                    return runCommand(conf, shard, ns, cmdObj, result);
                }

                ChunkManagerPtr chunkMgr = getChunkManager(conf, ns);

                const BSONObj query = cmdObj.getObjectField("query");
                StatusWith<BSONObj> status = getShardKey(chunkMgr, ns, query);
                // Bad query
                if (!status.isOK()) {
                    return appendCommandStatus(result, status.getStatus());
                }

                BSONObj shardKey = status.getValue();
                ChunkPtr chunk = chunkMgr->findIntersectingChunk(shardKey);
                Shard shard = chunk->getShard();

                bool ok = runCommand(conf, shard, ns, cmdObj, result);

                if (ok) {
                    // check whether split is necessary (using update object for size heuristic)
                    if (haveClient() && ClusterLastErrorInfo::get(cc()).autoSplitOk()) {
                        chunk->splitIfShould(cmdObj.getObjectField("update").objsize());
                    }
                }

                return ok;
            }

        private:
            ChunkManagerPtr getChunkManager(DBConfigPtr conf, const string& ns) const {
                ChunkManagerPtr chunkMgr = conf->getChunkManager(ns);
                massert(13002, "shard internal error chunk manager should never be null", chunkMgr);
                return chunkMgr;
            }

            StatusWith<BSONObj> getShardKey(ChunkManagerPtr chunkMgr,
                                            const string& ns,
                                            const BSONObj& query) const {
                // Verify that the query has an equality predicate using the shard key.
                StatusWith<BSONObj> status =
                    chunkMgr->getShardKeyPattern().extractShardKeyFromQuery(query);

                if (status.isOK()) {
                    BSONObj shardKey = status.getValue();
                    uassert(13343, "query for sharded findAndModify must have shardkey",
                            !shardKey.isEmpty());
                }
                return status;
            }

            bool runCommand(DBConfigPtr conf,
                            const Shard& shard,
                            const string& ns,
                            const BSONObj& cmdObj,
                            BSONObjBuilder& result) const {
                BSONObj res;

                ShardConnection conn(shard.getConnString(), ns);
                bool ok = conn->runCommand(conf->name(), cmdObj, res);
                conn.done();

                // RecvStaleConfigCode is the code for RecvStaleConfigException.
                if (!ok && res.getIntField("code") == RecvStaleConfigCode) {
                    // Command code traps this exception and re-runs
                    throw RecvStaleConfigException("FindAndModify", res);
                }

                result.appendElements(res);
                return ok;
            }

        } findAndModifyCmd;

        class DataSizeCmd : public PublicGridCommand {
        public:
            DataSizeCmd() : PublicGridCommand("dataSize", "datasize") { }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::find);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullns = parseNs(dbName, cmdObj);

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    return passthrough( conf , cmdObj , result);
                }

                ChunkManagerPtr cm = conf->getChunkManager( fullns );
                massert( 13407 ,  "how could chunk manager be null!" , cm );

                BSONObj min = cmdObj.getObjectField( "min" );
                BSONObj max = cmdObj.getObjectField( "max" );
                BSONObj keyPattern = cmdObj.getObjectField( "keyPattern" );

                uassert( 13408, "keyPattern must equal shard key",
                         cm->getShardKeyPattern().toBSON() == keyPattern );
                uassert( 13405, str::stream() << "min value " << min << " does not have shard key",
                         cm->getShardKeyPattern().isShardKey(min) );
                uassert( 13406, str::stream() << "max value " << max << " does not have shard key",
                         cm->getShardKeyPattern().isShardKey(max) );

                min = cm->getShardKeyPattern().normalizeShardKey(min);
                max = cm->getShardKeyPattern().normalizeShardKey(max);

                // yes these are doubles...
                double size = 0;
                double numObjects = 0;
                int millis = 0;

                set<Shard> shards;
                cm->getShardsForRange(shards, min, max);
                for ( set<Shard>::iterator i=shards.begin(), end=shards.end() ; i != end; ++i ) {
                    ScopedDbConnection conn(i->getConnString());
                    BSONObj res;
                    bool ok = conn->runCommand( conf->name() , cmdObj , res );
                    conn.done();

                    if ( ! ok ) {
                        result.appendElements( res );
                        return false;
                    }

                    size       += res["size"].number();
                    numObjects += res["numObjects"].number();
                    millis     += res["millis"].numberInt();

                }

                result.append( "size", size );
                result.append( "numObjects" , numObjects );
                result.append( "millis" , millis );
                return true;
            }

        } DataSizeCmd;

        class ConvertToCappedCmd : public NotAllowedOnShardedCollectionCmd  {
        public:
            ConvertToCappedCmd() : NotAllowedOnShardedCollectionCmd("convertToCapped") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::convertToCapped);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

        } convertToCappedCmd;


        class GroupCmd : public NotAllowedOnShardedCollectionCmd  {
        public:
            GroupCmd() : NotAllowedOnShardedCollectionCmd("group") {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::find);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            virtual bool passOptions() const { return true; }

            virtual std::string parseNs(const std::string& dbName, const BSONObj& cmdObj) const {
                return dbName + "." + cmdObj.firstElement()
                                            .embeddedObjectUserCheck()["ns"]
                                            .valuestrsafe();
            }

            Status explain(OperationContext* txn,
                           const std::string& dbname,
                           const BSONObj& cmdObj,
                           ExplainCommon::Verbosity verbosity,
                           BSONObjBuilder* out) const {
                const string fullns = parseNs(dbname, cmdObj);

                BSONObjBuilder explainCmdBob;
                ClusterExplain::wrapAsExplain(cmdObj, verbosity, &explainCmdBob);

                // We will time how long it takes to run the commands on the shards.
                Timer timer;

                Strategy::CommandResult singleResult;
                Status commandStat = STRATEGY->commandOpUnsharded(dbname,
                                                                  explainCmdBob.obj(),
                                                                  0,
                                                                  fullns,
                                                                  &singleResult);
                if (!commandStat.isOK()) {
                    return commandStat;
                }

                long long millisElapsed = timer.millis();

                vector<Strategy::CommandResult> shardResults;
                shardResults.push_back(singleResult);

                return ClusterExplain::buildExplainResult(shardResults,
                                                          ClusterExplain::kSingleShard,
                                                          millisElapsed,
                                                          out);
            }

        } groupCmd;

        class SplitVectorCmd : public NotAllowedOnShardedCollectionCmd  {
        public:
            SplitVectorCmd() : NotAllowedOnShardedCollectionCmd("splitVector") {}
            virtual bool passOptions() const { return true; }
            virtual Status checkAuthForCommand(ClientBasic* client,
                                               const std::string& dbname,
                                               const BSONObj& cmdObj) {
                if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                        ResourcePattern::forExactNamespace(NamespaceString(parseNs(dbname,
                                                                                   cmdObj))),
                        ActionType::splitVector)) {
                    return Status(ErrorCodes::Unauthorized, "Unauthorized");
                }
                return Status::OK();
            }
            virtual bool run(OperationContext* txn,
                             const string& dbName,
                             BSONObj& cmdObj,
                             int options,
                             string& errmsg,
                             BSONObjBuilder& result) {
                string x = parseNs(dbName, cmdObj);
                if ( ! str::startsWith( x , dbName ) ) {
                    errmsg = str::stream() << "doing a splitVector across dbs isn't supported via mongos";
                    return false;
                }
                return NotAllowedOnShardedCollectionCmd::run(txn,
                                                             dbName,
                                                             cmdObj,
                                                             options,
                                                             errmsg,
                                                             result);
            }
            virtual std::string parseNs(const string& dbname, const BSONObj& cmdObj) const {
                return parseNsFullyQualified(dbname, cmdObj);
            }

        } splitVectorCmd;


        class DistinctCmd : public PublicGridCommand {
        public:
            DistinctCmd() : PublicGridCommand("distinct") {}
            virtual void help( stringstream &help ) const {
                help << "{ distinct : 'collection name' , key : 'a.b' , query : {} }";
            }
            virtual bool passOptions() const { return true; }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::find);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            bool run(OperationContext* txn,
                     const string& dbName ,
                     BSONObj& cmdObj,
                     int options,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullns = parseNs(dbName, cmdObj);

                auto status = grid.catalogCache()->getDatabase(dbName);
                if (!status.isOK()) {
                    return appendEmptyResultSet(result, status.getStatus(), fullns);
                }

                shared_ptr<DBConfig> conf = status.getValue();
                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    return passthrough(conf, cmdObj, options, result);
                }

                ChunkManagerPtr cm = conf->getChunkManager( fullns );
                massert( 10420 ,  "how could chunk manager be null!" , cm );

                BSONObj query = getQuery(cmdObj);
                set<Shard> shards;
                cm->getShardsForQuery(shards, query);

                set<BSONObj,BSONObjCmp> all;
                int size = 32;

                for ( set<Shard>::iterator i=shards.begin(), end=shards.end() ; i != end; ++i ) {
                    ShardConnection conn(i->getConnString(), fullns);
                    BSONObj res;
                    bool ok = conn->runCommand( conf->name() , cmdObj , res, options );
                    conn.done();

                    if ( ! ok ) {
                        result.appendElements( res );
                        return false;
                    }

                    BSONObjIterator it( res["values"].embeddedObject() );
                    while ( it.more() ) {
                        BSONElement nxt = it.next();
                        BSONObjBuilder temp(32);
                        temp.appendAs( nxt , "" );
                        all.insert( temp.obj() );
                    }

                }

                BSONObjBuilder b( size );
                int n=0;
                for ( set<BSONObj,BSONObjCmp>::iterator i = all.begin() ; i != all.end(); i++ ) {
                    b.appendAs( i->firstElement() , b.numStr( n++ ) );
                }

                result.appendArray( "values" , b.obj() );
                return true;
            }
        } disinctCmd;

        class FileMD5Cmd : public PublicGridCommand {
        public:
            FileMD5Cmd() : PublicGridCommand("filemd5") {}
            virtual void help( stringstream &help ) const {
                help << " example: { filemd5 : ObjectId(aaaaaaa) , root : \"fs\" }";
            }

            virtual std::string parseNs(const std::string& dbname, const BSONObj& cmdObj) const {
                std::string collectionName = cmdObj.getStringField("root");
                if (collectionName.empty())
                    collectionName = "fs";
                collectionName += ".chunks";
                return NamespaceString(dbname, collectionName).ns();
            }

            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), ActionType::find));
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullns = parseNs(dbName, cmdObj);

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    return passthrough( conf , cmdObj , result );
                }

                ChunkManagerPtr cm = conf->getChunkManager( fullns );
                massert( 13091 , "how could chunk manager be null!" , cm );
                if(cm->getShardKeyPattern().toBSON() == BSON("files_id" << 1)) {
                    BSONObj finder = BSON("files_id" << cmdObj.firstElement());

                    vector<Strategy::CommandResult> results;
                    STRATEGY->commandOp(dbName, cmdObj, 0, fullns, finder, &results);
                    verify(results.size() == 1); // querying on shard key so should only talk to one shard
                    BSONObj res = results.begin()->result;

                    result.appendElements(res);
                    return res["ok"].trueValue();
                }
                else if (cm->getShardKeyPattern().toBSON() == BSON("files_id" << 1 << "n" << 1)) {
                    int n = 0;
                    BSONObj lastResult;

                    while (true) {
                        // Theory of operation: Starting with n=0, send filemd5 command to shard
                        // with that chunk (gridfs chunk not sharding chunk). That shard will then
                        // compute a partial md5 state (passed in the "md5state" field) for all
                        // contiguous chunks that it has. When it runs out or hits a discontinuity
                        // (eg [1,2,7]) it returns what it has done so far. This is repeated as
                        // long as we keep getting more chunks. The end condition is when we go to
                        // look for chunk n and it doesn't exist. This means that the file's last
                        // chunk is n-1, so we return the computed md5 results.
                        BSONObjBuilder bb;
                        bb.appendElements(cmdObj);
                        bb.appendBool("partialOk", true);
                        bb.append("startAt", n);
                        if (!lastResult.isEmpty()){
                            bb.append(lastResult["md5state"]);
                        }
                        BSONObj shardCmd = bb.obj();

                        BSONObj finder = BSON("files_id" << cmdObj.firstElement() << "n" << n);

                        vector<Strategy::CommandResult> results;
                        try {
                            STRATEGY->commandOp(dbName, shardCmd, 0, fullns, finder, &results);
                        }
                        catch( DBException& e ){
                            //This is handled below and logged
                            Strategy::CommandResult errResult;
                            errResult.shardTarget = Shard();
                            errResult.result = BSON("errmsg" << e.what() << "ok" << 0 );
                            results.push_back( errResult );
                        }

                        verify(results.size() == 1); // querying on shard key so should only talk to one shard
                        BSONObj res = results.begin()->result;
                        bool ok = res["ok"].trueValue();

                        if (!ok) {
                            // Add extra info to make debugging easier
                            result.append("failedAt", n);
                            result.append("sentCommand", shardCmd);
                            BSONForEach(e, res){
                                if (!str::equals(e.fieldName(), "errmsg"))
                                    result.append(e);
                            }

                            log() << "Sharded filemd5 failed: " << result.asTempObj() << endl;

                            errmsg = string("sharded filemd5 failed because: ") + res["errmsg"].valuestrsafe();
                            return false;
                        }

                        uassert(16246, "Shard " + conf->name() + " is too old to support GridFS sharded by {files_id:1, n:1}",
                                res.hasField("md5state"));

                        lastResult = res;
                        int nNext = res["numChunks"].numberInt();

                        if (n == nNext){
                            // no new data means we've reached the end of the file
                            result.appendElements(res);
                            return true;
                        }
                            
                        verify(nNext > n);
                        n = nNext;
                    }

                    verify(0);
                }

                // We could support arbitrary shard keys by sending commands to all shards but I don't think we should
                errmsg = "GridFS fs.chunks collection must be sharded on either {files_id:1} or {files_id:1, n:1}";
                return false;
            }
        } fileMD5Cmd;

        class Geo2dFindNearCmd : public PublicGridCommand {
        public:
            Geo2dFindNearCmd() : PublicGridCommand( "geoNear" ) {}
            void help(stringstream& h) const { h << "http://dochub.mongodb.org/core/geo#GeospatialIndexing-geoNearCommand"; }
            virtual bool passOptions() const { return true; }
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::find);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int options,
                     string& errmsg,
                     BSONObjBuilder& result) {
                const string fullns = parseNs(dbName, cmdObj);

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                    return passthrough( conf , cmdObj , options, result );
                }

                ChunkManagerPtr cm = conf->getChunkManager( fullns );
                massert( 13500 ,  "how could chunk manager be null!" , cm );

                BSONObj query = getQuery(cmdObj);
                set<Shard> shards;
                cm->getShardsForQuery(shards, query);

                // We support both "num" and "limit" options to control limit
                int limit = 100;
                const char* limitName = cmdObj["num"].isNumber() ? "num" : "limit";
                if (cmdObj[limitName].isNumber())
                    limit = cmdObj[limitName].numberInt();

                list< shared_ptr<Future::CommandResult> > futures;
                BSONArrayBuilder shardArray;
                for ( set<Shard>::const_iterator i=shards.begin(), end=shards.end() ; i != end ; i++ ) {
                    futures.push_back( Future::spawnCommand( i->getConnString() , dbName , cmdObj, options ) );
                    shardArray.append(i->getName());
                }

                multimap<double, BSONObj> results; // TODO: maybe use merge-sort instead
                string nearStr;
                double time = 0;
                double btreelocs = 0;
                double nscanned = 0;
                double objectsLoaded = 0;
                for ( list< shared_ptr<Future::CommandResult> >::iterator i=futures.begin(); i!=futures.end(); i++ ) {
                    shared_ptr<Future::CommandResult> res = *i;
                    if ( ! res->join() ) {
                        errmsg = res->result()["errmsg"].String();
                        if (res->result().hasField("code")) {
                            result.append(res->result()["code"]);
                        }
                        return false;
                    }

                    if (res->result().hasField("near")) {
                        nearStr = res->result()["near"].String();
                    }
                    time += res->result()["stats"]["time"].Number();
                    if (!res->result()["stats"]["btreelocs"].eoo()) {
                        btreelocs += res->result()["stats"]["btreelocs"].Number();
                    }
                    nscanned += res->result()["stats"]["nscanned"].Number();
                    if (!res->result()["stats"]["objectsLoaded"].eoo()) {
                        objectsLoaded += res->result()["stats"]["objectsLoaded"].Number();
                    }

                    BSONForEach(obj, res->result()["results"].embeddedObject()) {
                        results.insert(make_pair(obj["dis"].Number(), obj.embeddedObject().getOwned()));
                    }

                    // TODO: maybe shrink results if size() > limit
                }

                result.append("ns" , fullns);
                result.append("near", nearStr);

                int outCount = 0;
                double totalDistance = 0;
                double maxDistance = 0;
                {
                    BSONArrayBuilder sub (result.subarrayStart("results"));
                    for (multimap<double, BSONObj>::const_iterator it(results.begin()), end(results.end()); it!= end && outCount < limit; ++it, ++outCount) {
                        totalDistance += it->first;
                        maxDistance = it->first; // guaranteed to be highest so far

                        sub.append(it->second);
                    }
                    sub.done();
                }

                {
                    BSONObjBuilder sub (result.subobjStart("stats"));
                    sub.append("time", time);
                    sub.append("btreelocs", btreelocs);
                    sub.append("nscanned", nscanned);
                    sub.append("objectsLoaded", objectsLoaded);
                    sub.append("avgDistance", (outCount == 0) ? 0: (totalDistance / outCount));
                    sub.append("maxDistance", maxDistance);
                    sub.append("shards", shardArray.arr());
                    sub.done();
                }

                return true;
            }
        } geo2dFindNearCmd;

        /**
         * Outline for sharded map reduce for sharded output, $out replace:
         *
         * ============= mongos =============
         * 1. Send map reduce command to all relevant shards with some extra info like
         *    the value for the chunkSize and the name of the temporary output collection.
         *
         * ============= shard =============
         * 2. Does normal map reduce.
         * 3. Calls splitVector on itself against the output collection and puts the results
         *    to the response object.
         *
         * ============= mongos =============
         * 4. If the output collection is *not* sharded, uses the information from splitVector
         *    to create a pre-split sharded collection.
         * 5. Grabs the distributed lock for the final output collection.
         * 6. Sends mapReduce.shardedfinish.
         *
         * ============= shard =============
         * 7. Extracts the list of shards from the mapReduce.shardedfinish and performs a
         *    broadcast query against all of them to obtain all documents that this shard owns.
         * 8. Performs the reduce operation against every document from step #7 and outputs them
         *    to another temporary collection. Also keeps track of the BSONObject size of
         *    the every "reduced" documents for each chunk range.
         * 9. Atomically drops the old output collection and renames the temporary collection to
         *    the output collection.
         *
         * ============= mongos =============
         * 10. Releases the distributed lock acquired at step #5.
         * 11. Inspects the BSONObject size from step #8 and determines if it needs to split.
         */
        class MRCmd : public PublicGridCommand {
        public:
            AtomicUInt32 JOB_NUMBER;

            MRCmd() : PublicGridCommand( "mapReduce", "mapreduce" ) {}

            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                mr::addPrivilegesRequiredForMapReduce(this, dbname, cmdObj, out);
            }

            string getTmpName( const string& coll ) {
                stringstream ss;
                ss << "tmp.mrs." << coll << "_" << time(0) << "_" << JOB_NUMBER.fetchAndAdd(1);
                return ss.str();
            }

            BSONObj fixForShards( const BSONObj& orig , const string& output , string& badShardedField , int maxChunkSizeBytes ) {
                BSONObjBuilder b;
                BSONObjIterator i( orig );
                while ( i.more() ) {
                    BSONElement e = i.next();
                    string fn = e.fieldName();
                    if (fn == "map" ||
                            fn == "mapreduce" ||
                            fn == "mapReduce" ||
                            fn == "mapparams" ||
                            fn == "reduce" ||
                            fn == "query" ||
                            fn == "sort" ||
                            fn == "scope" ||
                            fn == "verbose" ||
                            fn == "$queryOptions" ||
                            fn == LiteParsedQuery::cmdOptionMaxTimeMS) {
                        b.append( e );
                    }
                    else if ( fn == "out" ||
                              fn == "finalize" ) {
                        // we don't want to copy these
                    }
                    else {
                        badShardedField = fn;
                        return BSONObj();
                    }
                }
                b.append( "out" , output );
                b.append( "shardedFirstPass" , true );

                if ( maxChunkSizeBytes > 0 ) {
                    // will need to figure out chunks, ask shards for points
                    b.append("splitInfo", maxChunkSizeBytes);
                }

                return b.obj();
            }

            void cleanUp( const set<string>& servers, string dbName, string shardResultCollection ) {
                try {
                    // drop collections with tmp results on each shard
                    for ( set<string>::const_iterator i=servers.begin(); i!=servers.end(); i++ ) {
                        ScopedDbConnection conn(*i);
                        conn->dropCollection( dbName + "." + shardResultCollection );
                        conn.done();
                    }
                } catch ( std::exception e ) {
                    log() << "Cannot cleanup shard results" << causedBy( e ) << endl;
                }
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {
                return run( txn, dbName, cmdObj, errmsg, result, 0 );
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     string& errmsg,
                     BSONObjBuilder& result,
                     int retry) {
                Timer t;

                const string collection = cmdObj.firstElement().valuestrsafe();
                const string fullns = dbName + "." + collection;

                // Abort after two retries, m/r is an expensive operation
                if( retry > 2 ) {
                    errmsg = "shard version errors preventing parallel mapreduce, check logs for further info";
                    return false;
                }

                // Re-check shard version after 1st retry
                if( retry > 0 ) {
                    versionManager.forceRemoteCheckShardVersionCB( fullns );
                }

                const string shardResultCollection = getTmpName( collection );

                BSONObj customOut;
                string finalColShort;
                string finalColLong;
                bool customOutDB = false;
                string outDB = dbName;
                BSONElement outElmt = cmdObj.getField("out");
                if (outElmt.type() == Object) {
                    // check if there is a custom output
                    BSONObj out = outElmt.embeddedObject();
                    customOut = out;
                    // mode must be 1st element
                    finalColShort = out.firstElement().str();
                    if (customOut.hasField( "db" )) {
                        customOutDB = true;
                        outDB = customOut.getField("db").str();
                    }
                    finalColLong = outDB + "." + finalColShort;
                }

                // Ensure the input database exists
                auto status = grid.catalogCache()->getDatabase(dbName);
                if (!status.isOK()) {
                    return appendCommandStatus(result, status.getStatus());
                }

                shared_ptr<DBConfig> confIn = status.getValue();
                shared_ptr<DBConfig> confOut;

                if (customOutDB) {
                    // Create the output database implicitly
                    confOut = uassertStatusOK(grid.implicitCreateDb(outDB));
                }
                else {
                    confOut = confIn;
                }

                bool shardedInput = confIn && confIn->isShardingEnabled() && confIn->isSharded(fullns);
                bool shardedOutput = customOut.getBoolField("sharded");

                if (!shardedOutput) {
                    uassert(15920,
                            "Cannot output to a non-sharded collection because "
                                "sharded collection exists already",
                            !confOut->isSharded(finalColLong));

                    // TODO: Should we also prevent going from non-sharded to sharded? During the
                    //       transition client may see partial data.
                }

                long long maxChunkSizeBytes = 0;
                if (shardedOutput) {
                    // will need to figure out chunks, ask shards for points
                    maxChunkSizeBytes = cmdObj["maxChunkSizeBytes"].numberLong();
                    if ( maxChunkSizeBytes == 0 ) {
                        maxChunkSizeBytes = Chunk::MaxChunkSize;
                    }
                }

                if (customOut.hasField("inline") && shardedOutput) {
                    errmsg = "cannot specify inline and sharded output at the same time";
                    return false;
                }

                // modify command to run on shards with output to tmp collection
                string badShardedField;
                verify( maxChunkSizeBytes < 0x7fffffff );
                BSONObj shardedCommand = fixForShards(cmdObj,
                                                      shardResultCollection,
                                                      badShardedField,
                                                      static_cast<int>(maxChunkSizeBytes));

                if ( ! shardedInput && ! shardedOutput && ! customOutDB ) {
                    LOG(1) << "simple MR, just passthrough" << endl;
                    return passthrough( confIn , cmdObj , result );
                }

                if ( badShardedField.size() ) {
                    errmsg = str::stream() << "unknown m/r field for sharding: " << badShardedField;
                    return false;
                }

                BSONObjBuilder timingBuilder;
                BSONObj q;
                if ( cmdObj["query"].type() == Object ) {
                    q = cmdObj["query"].embeddedObjectUserCheck();
                }

                set<Shard> shards;
                set<string> servers;
                vector<Strategy::CommandResult> results;

                BSONObjBuilder shardResultsB;
                BSONObjBuilder shardCountsB;
                BSONObjBuilder aggCountsB;
                map<string,long long> countsMap;
                set< BSONObj > splitPts;
                BSONObj singleResult;
                bool ok = true;

                {
                    // TODO: take distributed lock to prevent split / migration?

                    try {
                        STRATEGY->commandOp( dbName, shardedCommand, 0, fullns, q, &results );
                    }
                    catch( DBException& e ){
                        e.addContext( str::stream() << "could not run map command on all shards for ns " << fullns << " and query " << q );
                        throw;
                    }

                    for ( vector<Strategy::CommandResult>::iterator i = results.begin();
                            i != results.end(); ++i ) {

                    	// need to gather list of all servers even if an error happened
                        const string server = i->shardTarget.getConnString();
                        servers.insert( server );
                        if ( !ok ) continue;

                        singleResult = i->result;
                        ok = singleResult["ok"].trueValue();
                        if ( !ok ) continue;

                        shardResultsB.append( server , singleResult );
                        BSONObj counts = singleResult["counts"].embeddedObjectUserCheck();
                        shardCountsB.append( server , counts );

                        // add up the counts for each shard
                        // some of them will be fixed later like output and reduce
                        BSONObjIterator j( counts );
                        while ( j.more() ) {
                            BSONElement temp = j.next();
                            countsMap[temp.fieldName()] += temp.numberLong();
                        }

                        if (singleResult.hasField("splitKeys")) {
                            BSONElement splitKeys = singleResult.getField("splitKeys");
                            vector<BSONElement> pts = splitKeys.Array();
                            for (vector<BSONElement>::iterator it = pts.begin(); it != pts.end(); ++it) {
                                splitPts.insert(it->Obj().getOwned());
                            }
                        }
                    }
                }

                if ( ! ok ) {
                    cleanUp( servers, dbName, shardResultCollection );
                    errmsg = "MR parallel processing failed: ";
                    errmsg += singleResult.toString();
                    // Add "code" to the top-level response, if the failure of the sharded command
                    // can be accounted to a single error.
                    int code = getUniqueCodeFromCommandResults( results );
                    if ( code != 0 ) {
                        result.append( "code", code );
                    }
                    return 0;
                }

                // build the sharded finish command
                BSONObjBuilder finalCmd;
                finalCmd.append( "mapreduce.shardedfinish" , cmdObj );
                finalCmd.append( "inputDB" , dbName );
                finalCmd.append( "shardedOutputCollection" , shardResultCollection );

                finalCmd.append( "shards" , shardResultsB.done() );
                BSONObj shardCounts = shardCountsB.done();
                finalCmd.append( "shardCounts" , shardCounts );
                timingBuilder.append( "shardProcessing" , t.millis() );

                for ( map<string,long long>::iterator i=countsMap.begin(); i!=countsMap.end(); i++ ) {
                    aggCountsB.append( i->first , i->second );
                }
                BSONObj aggCounts = aggCountsB.done();
                finalCmd.append( "counts" , aggCounts );

                if (cmdObj.hasField(LiteParsedQuery::cmdOptionMaxTimeMS)) {
                    finalCmd.append(cmdObj[LiteParsedQuery::cmdOptionMaxTimeMS]);
                }

                Timer t2;
                long long reduceCount = 0;
                long long outputCount = 0;
                BSONObjBuilder postCountsB;

                if (!shardedOutput) {
                    LOG(1) << "MR with single shard output, NS=" << finalColLong << " primary=" << confOut->getPrimary() << endl;
                    ShardConnection conn(confOut->getPrimary().getConnString(), finalColLong);
                    ok = conn->runCommand( outDB , finalCmd.obj() , singleResult );

                    BSONObj counts = singleResult.getObjectField("counts");
                    postCountsB.append(conn->getServerAddress(), counts);
                    reduceCount = counts.getIntField("reduce");
                    outputCount = counts.getIntField("output");

                    conn.done();
                } else {

                    LOG(1) << "MR with sharded output, NS=" << finalColLong << endl;

                    // create the sharded collection if needed
                    if (!confOut->isSharded(finalColLong)) {
                        // enable sharding on db
                        confOut->enableSharding();

                        // shard collection according to split points
                        BSONObj sortKey = BSON( "_id" << 1 );
                        vector<BSONObj> sortedSplitPts;
                        // points will be properly sorted using the set
                        for ( set<BSONObj>::iterator it = splitPts.begin() ; it != splitPts.end() ; ++it )
                            sortedSplitPts.push_back( *it );

                        // pre-split the collection onto all the shards for this database.
                        // Note that it's not completely safe to pre-split onto non-primary shards
                        // using the shardcollection method (a conflict may result if multiple
                        // map-reduces are writing to the same output collection, for instance).
                        // TODO: pre-split mapReduce output in a safer way.
                        set<Shard> shardSet;
                        confOut->getAllShards( shardSet );
                        vector<Shard> outShards( shardSet.begin() , shardSet.end() );

                        ShardKeyPattern sortKeyPattern(sortKey);
                        Status status = grid.catalogManager()->shardCollection(finalColLong,
                                                                               sortKeyPattern,
                                                                               true,
                                                                               &sortedSplitPts,
                                                                               &outShards);
                        if (!status.isOK()) {
                            return appendCommandStatus(result, status);
                        }

                    }

                    map<BSONObj, int> chunkSizes;
                    {
                        // take distributed lock to prevent split / migration.
                        auto scopedDistLock = grid.catalogManager()->getDistLockManager()->lock(
                                finalColLong,
                                "mr-post-process",
                                stdx::chrono::milliseconds(-1), // retry indefinitely
                                stdx::chrono::milliseconds(100));

                        if (!scopedDistLock.isOK()) {
                            return appendCommandStatus(result, scopedDistLock.getStatus());
                        }

                        BSONObj finalCmdObj = finalCmd.obj();
                        results.clear();

                        try {
                            STRATEGY->commandOp( outDB, finalCmdObj, 0, finalColLong, BSONObj(), &results );
                            ok = true;
                        }
                        catch( DBException& e ){
                            e.addContext( str::stream() << "could not run final reduce command on all shards for ns " << fullns << ", output " << finalColLong );
                            throw;
                        }

                        for ( vector<Strategy::CommandResult>::iterator i = results.begin();
                                i != results.end(); ++i ) {

                            string server = i->shardTarget.getConnString();
                            singleResult = i->result;
                            ok = singleResult["ok"].trueValue();
                            if ( !ok ) break;

                            BSONObj counts = singleResult.getObjectField("counts");
                            reduceCount += counts.getIntField("reduce");
                            outputCount += counts.getIntField("output");
                            postCountsB.append(server, counts);

                            // get the size inserted for each chunk
                            // split cannot be called here since we already have the distributed lock
                            if (singleResult.hasField("chunkSizes")) {
                                vector<BSONElement> sizes = singleResult.getField("chunkSizes").Array();
                                for (unsigned int i = 0; i < sizes.size(); i += 2) {
                                    BSONObj key = sizes[i].Obj().getOwned();
                                    long long size = sizes[i+1].numberLong();
                                    verify( size < 0x7fffffff );
                                    chunkSizes[key] = static_cast<int>(size);
                                }
                            }
                        }
                    }

                    // do the splitting round
                    ChunkManagerPtr cm = confOut->getChunkManagerIfExists( finalColLong );
                    for ( map<BSONObj, int>::iterator it = chunkSizes.begin() ; it != chunkSizes.end() ; ++it ) {
                        BSONObj key = it->first;
                        int size = it->second;
                        verify( size < 0x7fffffff );

                        // key reported should be the chunk's minimum
                        ChunkPtr c =  cm->findIntersectingChunk(key);
                        if ( !c ) {
                            warning() << "Mongod reported " << size << " bytes inserted for key " << key << " but can't find chunk" << endl;
                        } else {
                            c->splitIfShould( size );
                        }
                    }
                }

                cleanUp( servers, dbName, shardResultCollection );

                if ( ! ok ) {
                    errmsg = "MR post processing failed: ";
                    errmsg += singleResult.toString();
                    return 0;
                }

                // copy some elements from a single result
                // annoying that we have to copy all results for inline, but no way around it
                if (singleResult.hasField("result"))
                    result.append(singleResult.getField("result"));
                else if (singleResult.hasField("results"))
                    result.append(singleResult.getField("results"));

                BSONObjBuilder countsB(32);
                // input stat is determined by aggregate MR job
                countsB.append("input", aggCounts.getField("input").numberLong());
                countsB.append("emit", aggCounts.getField("emit").numberLong());

                // reduce count is sum of all reduces that happened
                countsB.append("reduce", aggCounts.getField("reduce").numberLong() + reduceCount);

                // ouput is determined by post processing on each shard
                countsB.append("output", outputCount);
                result.append( "counts" , countsB.done() );

                timingBuilder.append( "postProcessing" , t2.millis() );

                result.append( "timeMillis" , t.millis() );
                result.append( "timing" , timingBuilder.done() );
                result.append("shardCounts", shardCounts);
                result.append("postProcessCounts", postCountsB.done());
                return 1;
            }
        } mrCmd;

        class ApplyOpsCmd : public PublicGridCommand {
        public:
            ApplyOpsCmd() : PublicGridCommand( "applyOps" ) {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                // applyOps can do pretty much anything, so require all privileges.
                RoleGraph::generateUniversalPrivileges(out);
            }
            virtual bool run(OperationContext* txn, const string& dbName , BSONObj& cmdObj, int, string& errmsg, BSONObjBuilder& result) {
                errmsg = "applyOps not allowed through mongos";
                return false;
            }
        } applyOpsCmd;


        class CompactCmd : public PublicGridCommand {
        public:
            CompactCmd() : PublicGridCommand( "compact" ) {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                ActionSet actions;
                actions.addAction(ActionType::compact);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }
            virtual bool run(OperationContext* txn,
                             const string& dbName,
                             BSONObj& cmdObj,
                             int,
                             string& errmsg,
                             BSONObjBuilder& result) {
                errmsg = "compact not allowed through mongos";
                return false;
            }
        } compactCmd;

        class EvalCmd : public PublicGridCommand {
        public:
            EvalCmd() : PublicGridCommand( "eval", "$eval" ) {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                // $eval can do pretty much anything, so require all privileges.
                RoleGraph::generateUniversalPrivileges(out);
            }
            virtual bool run(OperationContext* txn,
                             const string& dbName,
                             BSONObj& cmdObj,
                             int,
                             string& errmsg,
                             BSONObjBuilder& result) {

                RARELY {
                    warning() << "the eval command is deprecated" << startupWarningsLog;
                }

                // $eval isn't allowed to access sharded collections, but we need to leave the
                // shard to detect that.
                auto status = grid.catalogCache()->getDatabase(dbName);
                if (!status.isOK()) {
                    return appendCommandStatus(result, status.getStatus());
                }

                shared_ptr<DBConfig> conf = status.getValue();
                return passthrough( conf , cmdObj , result );
            }
        } evalCmd;

        /*
          Note these are in the pub_grid_cmds namespace, so they don't
          conflict with those in db/commands/pipeline_command.cpp.
         */
        class PipelineCommand :
            public PublicGridCommand {
        public:
            PipelineCommand();
            // virtuals from Command
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out);
            virtual bool run(OperationContext* txn,
                             const string &dbName,
                             BSONObj &cmdObj,
                             int options,
                             string &errmsg,
                             BSONObjBuilder &result);

        private:
            DocumentSourceMergeCursors::CursorIds parseCursors(
                const vector<Strategy::CommandResult>& shardResults,
                const string& fullns);

            void killAllCursors(const vector<Strategy::CommandResult>& shardResults);
            bool doAnyShardsNotSupportCursors(const vector<Strategy::CommandResult>& shardResults);
            bool wasMergeCursorsSupported(BSONObj cmdResult);
            void uassertCanMergeInMongos(intrusive_ptr<Pipeline> mergePipeline, BSONObj cmdObj);
            void uassertAllShardsSupportExplain(
                const vector<Strategy::CommandResult>& shardResults);

            void noCursorFallback(intrusive_ptr<Pipeline> shardPipeline,
                                  intrusive_ptr<Pipeline> mergePipeline,
                                  const string& dbname,
                                  const string& fullns,
                                  int options,
                                  BSONObj cmdObj,
                                  BSONObjBuilder& result);

            // These are temporary hacks because the runCommand method doesn't report the exact
            // host the command was run on which is necessary for cursor support. The exact host
            // could be different from conn->getServerAddress() for connections that map to
            // multiple servers such as for replica sets. These also take care of registering
            // returned cursors with mongos's cursorCache.
            BSONObj aggRunCommand(DBClientBase* conn,
                                  const string& db,
                                  BSONObj cmd,
                                  int queryOptions);
            bool aggPassthrough(DBConfigPtr conf,
                                BSONObj cmd,
                                BSONObjBuilder& result,
                                int queryOptions);

        };


        /* -------------------- PipelineCommand ----------------------------- */

        static const PipelineCommand pipelineCommand;

        PipelineCommand::PipelineCommand():
            PublicGridCommand(Pipeline::commandName) {
        }

        void PipelineCommand::addRequiredPrivileges(const std::string& dbname,
                                                    const BSONObj& cmdObj,
                                                    std::vector<Privilege>* out) {
            Pipeline::addRequiredPrivileges(this, dbname, cmdObj, out);
        }

        bool PipelineCommand::run(OperationContext* txn,
                                  const string &dbName,
                                  BSONObj &cmdObj,
                                  int options,
                                  string &errmsg,
                                  BSONObjBuilder &result) {
            const string fullns = parseNs(dbName, cmdObj);

            intrusive_ptr<ExpressionContext> pExpCtx =
                new ExpressionContext(txn, NamespaceString(fullns));
            pExpCtx->inRouter = true;
            // explicitly *not* setting pExpCtx->tempDir

            /* parse the pipeline specification */
            intrusive_ptr<Pipeline> pPipeline(
                Pipeline::parseCommand(errmsg, cmdObj, pExpCtx));
            if (!pPipeline.get())
                return false; // there was some parsing error

            // If the system isn't running sharded, or the target collection isn't sharded, pass
            // this on to a mongod.
            auto status = grid.catalogCache()->getDatabase(dbName);
            if (!status.isOK()) {
                return appendEmptyResultSet(result, status.getStatus(), fullns);
            }

            shared_ptr<DBConfig> conf = status.getValue();

            if (!conf->isShardingEnabled() || !conf->isSharded(fullns)) {
                return aggPassthrough(conf, cmdObj, result, options);
            }

            /* split the pipeline into pieces for mongods and this mongos */
            intrusive_ptr<Pipeline> pShardPipeline(pPipeline->splitForSharded());

            // create the command for the shards
            MutableDocument commandBuilder(pShardPipeline->serialize());
            commandBuilder.setField("fromRouter", Value(true)); // this means produce output to be merged

            if (cmdObj.hasField("$queryOptions")) {
                commandBuilder.setField("$queryOptions", Value(cmdObj["$queryOptions"]));
            }

            if (!pPipeline->isExplain()) {
                // "cursor" is ignored by 2.6 shards when doing explain, but including it leads to a
                // worse error message when talking to 2.4 shards.
                commandBuilder.setField("cursor", Value(DOC("batchSize" << 0)));
            }

            if (cmdObj.hasField(LiteParsedQuery::cmdOptionMaxTimeMS)) {
                commandBuilder.setField(LiteParsedQuery::cmdOptionMaxTimeMS,
                                        Value(cmdObj[LiteParsedQuery::cmdOptionMaxTimeMS]));
            }

            BSONObj shardedCommand = commandBuilder.freeze().toBson();
            BSONObj shardQuery = pShardPipeline->getInitialQuery();

            // Run the command on the shards
            // TODO need to make sure cursors are killed if a retry is needed
            vector<Strategy::CommandResult> shardResults;
            STRATEGY->commandOp(dbName, shardedCommand, options, fullns, shardQuery, &shardResults);

            if (pPipeline->isExplain()) {
                // This must be checked before we start modifying result.
                uassertAllShardsSupportExplain(shardResults);

                result << "splitPipeline" << DOC("shardsPart" << pShardPipeline->writeExplainOps()
                                              << "mergerPart" << pPipeline->writeExplainOps());

                BSONObjBuilder shardExplains(result.subobjStart("shards"));
                for (size_t i = 0; i < shardResults.size(); i++) {
                    shardExplains.append(shardResults[i].shardTarget.getName(),
                                         BSON("host" << shardResults[i].target.toString()
                                           << "stages" << shardResults[i].result["stages"]));
                }
                        
                return true;
            }

            if (doAnyShardsNotSupportCursors(shardResults)) {
                killAllCursors(shardResults);
                noCursorFallback(
                        pShardPipeline, pPipeline, dbName, fullns, options, cmdObj, result);
                return true;
            }

            DocumentSourceMergeCursors::CursorIds cursorIds = parseCursors(shardResults, fullns);
            pPipeline->addInitialSource(DocumentSourceMergeCursors::create(cursorIds, pExpCtx));

            MutableDocument mergeCmd(pPipeline->serialize());

            if (cmdObj.hasField("cursor"))
                mergeCmd["cursor"] = Value(cmdObj["cursor"]);
            if (cmdObj.hasField("$queryOptions"))
                mergeCmd["$queryOptions"] = Value(cmdObj["$queryOptions"]);
            if (cmdObj.hasField(LiteParsedQuery::cmdOptionMaxTimeMS)) {
                mergeCmd[LiteParsedQuery::cmdOptionMaxTimeMS]
                    = Value(cmdObj[LiteParsedQuery::cmdOptionMaxTimeMS]);
            }

            string outputNsOrEmpty;
            if (DocumentSourceOut* out = dynamic_cast<DocumentSourceOut*>(pPipeline->output())) {
                outputNsOrEmpty = out->getOutputNs().ns();
            }

            // Run merging command on primary shard of database. Need to use ShardConnection so
            // that the merging mongod is sent the config servers on connection init.
            const string mergeServer = conf->getPrimary().getConnString();
            ShardConnection conn(mergeServer, outputNsOrEmpty);
            BSONObj mergedResults = aggRunCommand(conn.get(),
                                                  dbName,
                                                  mergeCmd.freeze().toBson(),
                                                  options);
            bool ok = mergedResults["ok"].trueValue();
            conn.done();

            if (!ok && !wasMergeCursorsSupported(mergedResults)) {
                // This means that the cursors were constructed on all shards containing data
                // needed for the pipeline, but the primary shard doesn't support merging them.
                uassertCanMergeInMongos(pPipeline, cmdObj);

                pPipeline->stitch();
                pPipeline->run(result);
                return true;
            }

            // Copy output from merging (primary) shard to the output object from our command.
            // Also, propagates errmsg and code if ok == false.
            result.appendElements(mergedResults);

            return ok;
        }

        void PipelineCommand::uassertCanMergeInMongos(intrusive_ptr<Pipeline> mergePipeline,
                                                      BSONObj cmdObj) {
            uassert(17020, "All shards must support cursors to get a cursor back from aggregation",
                    !cmdObj.hasField("cursor"));

            uassert(17021, "All shards must support cursors to support new features in aggregation",
                    mergePipeline->canRunInMongos());
        }

        void PipelineCommand::noCursorFallback(intrusive_ptr<Pipeline> shardPipeline,
                                               intrusive_ptr<Pipeline> mergePipeline,
                                               const string& dbName,
                                               const string& fullns,
                                               int options,
                                               BSONObj cmdObj,
                                               BSONObjBuilder& result) {
            uassertCanMergeInMongos(mergePipeline, cmdObj);

            MutableDocument commandBuilder(shardPipeline->serialize());
            commandBuilder["fromRouter"] = Value(true);

            if (cmdObj.hasField("$queryOptions")) {
                commandBuilder["$queryOptions"] = Value(cmdObj["$queryOptions"]);
            }
            BSONObj shardedCommand = commandBuilder.freeze().toBson();
            BSONObj shardQuery = shardPipeline->getInitialQuery();

            // Run the command on the shards
            vector<Strategy::CommandResult> shardResults;
            STRATEGY->commandOp(dbName, shardedCommand, options, fullns, shardQuery, &shardResults);

            mergePipeline->addInitialSource(
                    DocumentSourceCommandShards::create(shardResults, mergePipeline->getContext()));

            // Combine the shards' output and finish the pipeline
            mergePipeline->stitch();
            mergePipeline->run(result);
        }

        DocumentSourceMergeCursors::CursorIds PipelineCommand::parseCursors(
                const vector<Strategy::CommandResult>& shardResults,
                const string& fullns) {
            try {
                DocumentSourceMergeCursors::CursorIds cursors;
                for (size_t i = 0; i < shardResults.size(); i++) {
                    BSONObj result = shardResults[i].result;

                    if ( !result["ok"].trueValue() ) {
                        // If the failure of the sharded command can be accounted to a single error,
                        // throw a UserException with that error code; otherwise, throw with a
                        // location uassert code.
                        int errCode = getUniqueCodeFromCommandResults( shardResults );
                        if ( errCode == 0 ) {
                            errCode = 17022;
                        }
                        verify( errCode == result["code"].numberInt() || errCode == 17022 );
                        uasserted( errCode, str::stream()
                                             << "sharded pipeline failed on shard "
                                             << shardResults[i].shardTarget.getName() << ": "
                                             << result.toString() );
                    }

                    BSONObj cursor = result["cursor"].Obj();

                    massert(17023, str::stream()
                                    << "shard " << shardResults[i].shardTarget.getName()
                                    << " returned non-empty first batch",
                            cursor["firstBatch"].Obj().isEmpty());
                    massert(17024, str::stream()
                                    << "shard " << shardResults[i].shardTarget.getName()
                                    << " returned cursorId 0",
                            cursor["id"].Long() != 0);
                    massert(17025, str::stream()
                                    << "shard " << shardResults[i].shardTarget.getName()
                                    << " returned different ns: " << cursor["ns"],
                            cursor["ns"].String() == fullns);

                    cursors.push_back(make_pair(shardResults[i].target, cursor["id"].Long()));
                }

                return cursors;
            }
            catch (...) {
                // Need to clean up any cursors we successfully created on the shards
                killAllCursors(shardResults);
                throw;
            }
        }

        bool PipelineCommand::doAnyShardsNotSupportCursors(
                const vector<Strategy::CommandResult>& shardResults) {
            // Note: all other errors are handled elsewhere
            for (size_t i = 0; i < shardResults.size(); i++) {
                // This is the result of requesting a cursor on a mongod <2.6. Yes, the
                // unbalanced '"' is correct.
                if (shardResults[i].result["errmsg"].str() == "unrecognized field \"cursor") {
                    return true;
                }
            }

            return false;
        }

        void PipelineCommand::uassertAllShardsSupportExplain(
                const vector<Strategy::CommandResult>& shardResults) {
            for (size_t i = 0; i < shardResults.size(); i++) {
                    uassert(17403, str::stream() << "Shard " << shardResults[i].target.toString()
                                                 << " failed: " << shardResults[i].result,
                            shardResults[i].result["ok"].trueValue());

                    uassert(17404, str::stream() << "Shard " << shardResults[i].target.toString()
                                                 << " does not support $explain",
                            shardResults[i].result.hasField("stages"));
            }
        }

        bool PipelineCommand::wasMergeCursorsSupported(BSONObj cmdResult) {
            // Note: all other errors are returned directly
            // This is the result of using $mergeCursors on a mongod <2.6.
            const char* errmsg = "exception: Unrecognized pipeline stage name: '$mergeCursors'";
            return cmdResult["errmsg"].str() != errmsg;
        }

        void PipelineCommand::killAllCursors(const vector<Strategy::CommandResult>& shardResults) {
            // This function must ignore and log all errors. Callers expect a best-effort attempt at
            // cleanup without exceptions. If any cursors aren't cleaned up here, they will be
            // cleaned up automatically on the shard after 10 minutes anyway.

            for (size_t i = 0; i < shardResults.size(); i++) {
                try {
                    BSONObj result = shardResults[i].result;
                    if (!result["ok"].trueValue())
                        continue;

                    long long cursor = result["cursor"]["id"].Long();
                    if (!cursor)
                        continue;

                    ScopedDbConnection conn(shardResults[i].target);
                    conn->killCursor(cursor);
                    conn.done();
                }
                catch (const DBException& e) {
                    log() << "Couldn't kill aggregation cursor on shard: "
                          << shardResults[i].target
                          << " due to DBException: " << e.toString();
                }
                catch (const std::exception& e) {
                    log() << "Couldn't kill aggregation cursor on shard: "
                          << shardResults[i].target
                          << " due to std::exception: " << e.what();
                }
                catch (...) {
                    log() << "Couldn't kill aggregation cursor on shard: "
                          << shardResults[i].target
                          << " due to non-exception";
                }
            }
        }

        BSONObj PipelineCommand::aggRunCommand(DBClientBase* conn,
                                               const string& db,
                                               BSONObj cmd,
                                               int queryOptions) {
            // Temporary hack. See comment on declaration for details.

            massert(17016, "should only be running an aggregate command here",
                    str::equals(cmd.firstElementFieldName(), "aggregate"));

            scoped_ptr<DBClientCursor> cursor(conn->query(db + ".$cmd",
                                                          cmd,
                                                          -1, // nToReturn
                                                          0, // nToSkip
                                                          NULL, // fieldsToReturn
                                                          queryOptions));
            massert(17014, str::stream() << "aggregate command didn't return results on host: "
                                         << conn->toString(),
                    cursor && cursor->more());

            BSONObj result = cursor->nextSafe().getOwned();
            uassertStatusOK(storePossibleCursor(cursor->originalHost(), result));
            return result;
        }

        bool PipelineCommand::aggPassthrough(DBConfigPtr conf,
                                             BSONObj cmd,
                                             BSONObjBuilder& out,
                                             int queryOptions) {
            // Temporary hack. See comment on declaration for details.

            ShardConnection conn(conf->getPrimary().getConnString(), "");
            BSONObj result = aggRunCommand(conn.get(), conf->name(), cmd, queryOptions);
            conn.done();

            bool ok = result["ok"].trueValue();
            if (!ok && result["code"].numberInt() == SendStaleConfigCode) {
                throw RecvStaleConfigException("command failed because of stale config", result);
            }
            out.appendElements(result);
            return ok;
        }

        class CmdListCollections : public PublicGridCommand {
        public:
            CmdListCollections() : PublicGridCommand( "listCollections" ) {}

            virtual Status checkAuthForCommand(ClientBasic* client,
                                           const std::string& dbname,
                                           const BSONObj& cmdObj) {
                AuthorizationSession* authzSession = AuthorizationSession::get(client);

                // Check for the listCollections ActionType on the database
                // or find on system.namespaces for pre 3.0 systems.
                if (authzSession->isAuthorizedForActionsOnResource(
                        ResourcePattern::forDatabaseName(dbname),
                        ActionType::listCollections) ||
                    authzSession->isAuthorizedForActionsOnResource(
                        ResourcePattern::forExactNamespace(
                            NamespaceString(dbname, "system.namespaces")),
                        ActionType::find)) {
                    return Status::OK();
                }

                return Status(ErrorCodes::Unauthorized,
                            str::stream() << "Not authorized to create users on db: " <<
                            dbname);
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int,
                     string& errmsg,
                     BSONObjBuilder& result) {

                auto status = grid.catalogCache()->getDatabase(dbName);
                if (!status.isOK()) {
                    return appendEmptyResultSet(result,
                                                status.getStatus(),
                                                dbName + ".$cmd.listCollections");
                }

                shared_ptr<DBConfig> conf = status.getValue();
                bool retval = passthrough( conf, cmdObj, result );

                Status storeCursorStatus = storePossibleCursor(conf->getPrimary().getConnString(),
                                                               result.asTempObj());
                if (!storeCursorStatus.isOK()) {
                    return appendCommandStatus(result, storeCursorStatus);
                }

                return retval;
            }
        } cmdListCollections;

        class CmdListIndexes : public PublicGridCommand {
        public:
            CmdListIndexes() : PublicGridCommand( "listIndexes" ) {}
            virtual void addRequiredPrivileges(const std::string& dbname,
                                               const BSONObj& cmdObj,
                                               std::vector<Privilege>* out) {
                string ns = parseNs( dbname, cmdObj );
                ActionSet actions;
                actions.addAction(ActionType::listIndexes);
                out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
            }

            bool run(OperationContext* txn,
                     const string& dbName,
                     BSONObj& cmdObj,
                     int options,
                     string& errmsg,
                     BSONObjBuilder& result) {

                auto conf = uassertStatusOK(grid.catalogCache()->getDatabase(dbName));
                bool retval = passthrough( conf, cmdObj, result );

                Status storeCursorStatus = storePossibleCursor(conf->getPrimary().getConnString(),
                                                               result.asTempObj());
                if (!storeCursorStatus.isOK()) {
                    return appendCommandStatus(result, storeCursorStatus);
                }

                return retval;
            }
        } cmdListIndexes;

        class AvailableQueryOptions : public Command {
        public:
          AvailableQueryOptions(): Command("availableQueryOptions",
                                           false ,
                                           "availablequeryoptions") {
          }

          virtual bool slaveOk() const { return true; }
          virtual bool isWriteCommandForConfigServer() const { return false; }
          virtual Status checkAuthForCommand(ClientBasic* client,
                                             const std::string& dbname,
                                             const BSONObj& cmdObj) {
              return Status::OK();
          }


          virtual bool run(OperationContext* txn,
                           const string& dbname,
                           BSONObj& cmdObj,
                           int,
                           string& errmsg,
                           BSONObjBuilder& result) {
              result << "options" << QueryOption_AllSupportedForSharding;
              return true;
          }
        } availableQueryOptionsCmd;

    } // namespace pub_grid_cmds

} // namespace mongo

