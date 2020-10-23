/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/s/config/sharding_catalog_manager.h"
#include "mongo/s/catalog/sharding_catalog_client_impl.h"
#include "mongo/s/grid.h"
#include "mongo/s/request_types/rename_collection_gen.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {
/**
 * Internal command run on config servers to rename a collection.
 */
class ConfigSvrRenameCollectionCommand final
    : public TypedCommand<ConfigSvrRenameCollectionCommand> {
public:
    using Request = ConfigsvrRenameCollection;

    class Invocation final : public InvocationBase {
    public:
        BSONObj _requestBody;
        Invocation(OperationContext* o, const Command* command, const OpMsgRequest& opMsgRequest)
            : InvocationBase(o, command, opMsgRequest) {
            _requestBody = opMsgRequest.body;
        }

        void typedRun(OperationContext* opCtx) {
            const NamespaceString& nssSource = ns();
            const NamespaceString& nssTarget = request().getTo();
            uassert(ErrorCodes::IllegalOperation,
                    "_configsvrRenameCollection can only be run on config servers",
                    serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

            // Set the operation context read concern level to local for reads into the config
            // database.
            repl::ReadConcernArgs::get(opCtx) =
                repl::ReadConcernArgs(repl::ReadConcernLevel::kLocalReadConcern);

            const auto catalogClient = Grid::get(opCtx)->catalogClient();
            auto sourceColl =
                uassertStatusOK(catalogClient->getCollection(
                                    opCtx, ns(), repl::ReadConcernLevel::kLocalReadConcern))
                    .value;
            uassert(ErrorCodes::InternalError, "Expected UUID", sourceColl.getUUID());

            auto scopedDbLockSource =
                ShardingCatalogManager::get(opCtx)->serializeCreateOrDropDatabase(opCtx,
                                                                                  nssSource.db());
            auto scopedCollLockSource =
                ShardingCatalogManager::get(opCtx)->serializeCreateOrDropCollection(opCtx,
                                                                                    nssSource);
            auto scopedCollLockTarget =
                ShardingCatalogManager::get(opCtx)->serializeCreateOrDropCollection(opCtx,
                                                                                    nssTarget);

            auto dbDistLockSource = uassertStatusOK(catalogClient->getDistLockManager()->lock(
                opCtx, nssSource.db(), "renameCollection", DistLockManager::kDefaultLockTimeout));
            auto collDistLockSource = uassertStatusOK(catalogClient->getDistLockManager()->lock(
                opCtx, nssSource.ns(), "renameCollection", DistLockManager::kDefaultLockTimeout));
            auto collDistLockTarget = uassertStatusOK(catalogClient->getDistLockManager()->lock(
                opCtx, nssTarget.ns(), "renameCollection", DistLockManager::kDefaultLockTimeout));

            ShardingCatalogManager::get(opCtx)->renameCollection(
                opCtx, request(), *sourceColl.getUUID(), _requestBody);
        }

    private:
        NamespaceString ns() const override {
            return request().getRenameCollection();
        }

        bool supportsWriteConcern() const override {
            return true;
        }

        void doCheckAuthorization(OperationContext* opCtx) const override {
            uassert(ErrorCodes::Unauthorized,
                    "Unauthorized",
                    AuthorizationSession::get(opCtx->getClient())
                        ->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(),
                                                           ActionType::internal));
        }
    };

    std::string help() const override {
        return "Internal command, which is exported by the sharding config server."
               "Renames a collection";
    }

    bool adminOnly() const override {
        return true;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }
} configSvrRenameCollectionCmd;

}  // namespace
}  // namespace mongo
