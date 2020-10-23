/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/client/embedded/service_context_embedded.h"
#include "mongo/client/embedded/service_entry_point_embedded.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/lock_state.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context_registrar.h"
#include "mongo/db/storage/storage_engine.h"
#include "mongo/db/storage/storage_engine_lock_file.h"
#include "mongo/db/storage/storage_engine_metadata.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"
#include "mongo/util/map_util.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/system_clock_source.h"
#include "mongo/util/system_tick_source.h"

namespace mongo {
namespace {
ServiceContextRegistrar serviceContextCreator([]() {
    auto service = std::make_unique<ServiceContextMongoEmbedded>();
    service->setServiceEntryPoint(std::make_unique<ServiceEntryPointEmbedded>(service.get()));
    service->setTickSource(std::make_unique<SystemTickSource>());
    service->setFastClockSource(std::make_unique<SystemClockSource>());
    service->setPreciseClockSource(std::make_unique<SystemClockSource>());
    return service;
});
}  // namespace

extern bool _supportsDocLocking;

ServiceContextMongoEmbedded::ServiceContextMongoEmbedded() = default;

ServiceContextMongoEmbedded::~ServiceContextMongoEmbedded() = default;

StorageEngine* ServiceContextMongoEmbedded::getGlobalStorageEngine() {
    // We don't check that globalStorageEngine is not-NULL here intentionally.  We can encounter
    // an error before it's initialized and proceed to exitCleanly which is equipped to deal
    // with a NULL storage engine.
    return _storageEngine.get();
}

void ServiceContextMongoEmbedded::createLockFile() {
    try {
        _lockFile = stdx::make_unique<StorageEngineLockFile>(storageGlobalParams.dbpath);
    } catch (const std::exception& ex) {
        uassert(50668,
                str::stream() << "Unable to determine status of lock file in the data directory "
                              << storageGlobalParams.dbpath
                              << ": "
                              << ex.what(),
                false);
    }
    bool wasUnclean = _lockFile->createdByUncleanShutdown();
    auto openStatus = _lockFile->open();
    if (storageGlobalParams.readOnly && openStatus == ErrorCodes::IllegalOperation) {
        _lockFile.reset();
    } else {
        uassertStatusOK(openStatus);
    }

    if (wasUnclean) {
        if (storageGlobalParams.readOnly) {
            severe() << "Attempted to open dbpath in readOnly mode, but the server was "
                        "previously not shut down cleanly.";
            fassertFailedNoTrace(50669);
        }
        warning() << "Detected unclean shutdown - " << _lockFile->getFilespec() << " is not empty.";
    }
}

void ServiceContextMongoEmbedded::initializeGlobalStorageEngine() {
    // This should be set once.
    invariant(!_storageEngine);

    // We should have a _lockFile or be in read-only mode. Confusingly, we can still have a lockFile
    // if we are in read-only mode. This can happen if the server is started in read-only mode on a
    // writable dbpath.
    invariant(_lockFile || storageGlobalParams.readOnly);

    const std::string dbpath = storageGlobalParams.dbpath;
    if (auto existingStorageEngine = StorageEngineMetadata::getStorageEngineForPath(dbpath)) {
        if (*existingStorageEngine == "mmapv1" ||
            (storageGlobalParams.engineSetByUser && storageGlobalParams.engine == "mmapv1")) {
            log() << startupWarningsLog;
            log() << "** WARNING: Support for MMAPV1 storage engine has been deprecated and will be"
                  << startupWarningsLog;
            log() << "**          removed in version 4.0. Please plan to migrate to the wiredTiger"
                  << startupWarningsLog;
            log() << "**          storage engine." << startupWarningsLog;
            log() << "**          See http://dochub.mongodb.org/core/deprecated-mmapv1";
            log() << startupWarningsLog;
        }

        if (storageGlobalParams.engineSetByUser) {
            // Verify that the name of the user-supplied storage engine matches the contents of
            // the metadata file.
            const StorageEngine::Factory* factory = nullptr;
            auto it = _storageFactories.find(storageGlobalParams.engine);
            if (it != _storageFactories.end())
                factory = it->second.get();

            if (factory) {
                uassert(50667,
                        str::stream() << "Cannot start server. Detected data files in " << dbpath
                                      << " created by"
                                      << " the '"
                                      << *existingStorageEngine
                                      << "' storage engine, but the"
                                      << " specified storage engine was '"
                                      << factory->getCanonicalName()
                                      << "'.",
                        factory->getCanonicalName() == *existingStorageEngine);
            }
        } else {
            // Otherwise set the active storage engine as the contents of the metadata file.
            log() << "Detected data files in " << dbpath << " created by the '"
                  << *existingStorageEngine << "' storage engine, so setting the active"
                  << " storage engine to '" << *existingStorageEngine << "'.";
            storageGlobalParams.engine = *existingStorageEngine;
        }
    } else if (!storageGlobalParams.engineSetByUser) {
        // Ensure the default storage engine is available with this build of mongod.
        uassert(50683,
                str::stream()
                    << "Cannot start server. The default storage engine '"
                    << storageGlobalParams.engine
                    << "' is not available with this build of mongod. Please specify a different"
                    << " storage engine explicitly, e.g. --storageEngine=mmapv1.",
                isRegisteredStorageEngine(storageGlobalParams.engine));
    } else if (storageGlobalParams.engineSetByUser && storageGlobalParams.engine == "mmapv1") {
        log() << startupWarningsLog;
        log() << "** WARNING: You have explicitly specified 'MMAPV1' storage engine in your"
              << startupWarningsLog;
        log() << "**          config file or as a command line option.  Support for the MMAPV1"
              << startupWarningsLog;
        log() << "**          storage engine has been deprecated and will be removed in"
              << startupWarningsLog;
        log() << "**          version 4.0. See http://dochub.mongodb.org/core/deprecated-mmapv1";
        log() << startupWarningsLog;
    }

    const std::string repairpath = storageGlobalParams.repairpath;
    uassert(50682,
            str::stream() << "Cannot start server. The command line option '--repairpath'"
                          << " is only supported by the mmapv1 storage engine",
            repairpath.empty() || repairpath == dbpath || storageGlobalParams.engine == "mmapv1");

    const auto& factory = _storageFactories[storageGlobalParams.engine];

    uassert(50681,
            str::stream() << "Cannot start server with an unknown storage engine: "
                          << storageGlobalParams.engine,
            factory);

    if (storageGlobalParams.readOnly) {
        uassert(50679,
                str::stream()
                    << "Server was started in read-only mode, but the configured storage engine, "
                    << storageGlobalParams.engine
                    << ", does not support read-only operation",
                factory->supportsReadOnly());
    }

    std::unique_ptr<StorageEngineMetadata> metadata = StorageEngineMetadata::forPath(dbpath);

    if (storageGlobalParams.readOnly) {
        uassert(50680,
                "Server was started in read-only mode, but the storage metadata file was not"
                " found.",
                metadata.get());
    }

    // Validate options in metadata against current startup options.
    if (metadata.get()) {
        uassertStatusOK(factory->validateMetadata(*metadata, storageGlobalParams));
    }

    ScopeGuard guard = MakeGuard([&] {
        if (_lockFile) {
            _lockFile->close();
        }
    });

    _storageEngine.reset(factory->create(storageGlobalParams, _lockFile.get()));
    _storageEngine->finishInit();

    if (_lockFile) {
        uassertStatusOK(_lockFile->writePid());
    }

    // Write a new metadata file if it is not present.
    if (!metadata.get()) {
        invariant(!storageGlobalParams.readOnly);
        metadata.reset(new StorageEngineMetadata(storageGlobalParams.dbpath));
        metadata->setStorageEngine(factory->getCanonicalName().toString());
        metadata->setStorageEngineOptions(factory->createMetadataOptions(storageGlobalParams));
        uassertStatusOK(metadata->write());
    }

    guard.Dismiss();

    _supportsDocLocking = _storageEngine->supportsDocLocking();
}

void ServiceContextMongoEmbedded::shutdownGlobalStorageEngineCleanly() {
    invariant(_storageEngine);
    _storageEngine->cleanShutdown();
    if (_lockFile) {
        _lockFile->clearPidAndUnlock();
    }
}

void ServiceContextMongoEmbedded::registerStorageEngine(const std::string& name,
                                                        const StorageEngine::Factory* factory) {
    // No double-registering.
    invariant(0 == _storageFactories.count(name));

    // Some sanity checks: the factory must exist,
    invariant(factory);

    // and all factories should be added before we pick a storage engine.
    invariant(NULL == _storageEngine);

    _storageFactories[name].reset(factory);
}

bool ServiceContextMongoEmbedded::isRegisteredStorageEngine(const std::string& name) {
    return _storageFactories.count(name);
}

StorageFactoriesIterator* ServiceContextMongoEmbedded::makeStorageFactoriesIterator() {
    return new StorageFactoriesIteratorMongoEmbedded(_storageFactories.begin(),
                                                     _storageFactories.end());
}

StorageFactoriesIteratorMongoEmbedded::StorageFactoriesIteratorMongoEmbedded(
    const FactoryMapIterator& begin, const FactoryMapIterator& end)
    : _curr(begin), _end(end) {}

bool StorageFactoriesIteratorMongoEmbedded::more() const {
    return _curr != _end;
}

const StorageEngine::Factory* StorageFactoriesIteratorMongoEmbedded::next() {
    return _curr++->second.get();
}

std::unique_ptr<OperationContext> ServiceContextMongoEmbedded::_newOpCtx(Client* client,
                                                                         unsigned opId) {
    invariant(&cc() == client);
    auto opCtx = stdx::make_unique<OperationContext>(client, opId);

    if (isMMAPV1()) {
        opCtx->setLockState(stdx::make_unique<MMAPV1LockerImpl>());
    } else {
        opCtx->setLockState(stdx::make_unique<DefaultLockerImpl>());
    }

    opCtx->setRecoveryUnit(getGlobalStorageEngine()->newRecoveryUnit(),
                           WriteUnitOfWork::RecoveryUnitState::kNotInUnitOfWork);
    return opCtx;
}

}  // namespace mongo
