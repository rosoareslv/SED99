/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.shield.client;

import org.elasticsearch.action.ActionFuture;
import org.elasticsearch.action.ActionListener;
import org.elasticsearch.client.ElasticsearchClient;
import org.elasticsearch.common.bytes.BytesReference;
import org.elasticsearch.shield.action.realm.ClearRealmCacheAction;
import org.elasticsearch.shield.action.realm.ClearRealmCacheRequest;
import org.elasticsearch.shield.action.realm.ClearRealmCacheRequestBuilder;
import org.elasticsearch.shield.action.realm.ClearRealmCacheResponse;
import org.elasticsearch.shield.action.role.ClearRolesCacheAction;
import org.elasticsearch.shield.action.role.ClearRolesCacheRequest;
import org.elasticsearch.shield.action.role.ClearRolesCacheRequestBuilder;
import org.elasticsearch.shield.action.role.ClearRolesCacheResponse;
import org.elasticsearch.shield.action.role.DeleteRoleAction;
import org.elasticsearch.shield.action.role.DeleteRoleRequest;
import org.elasticsearch.shield.action.role.DeleteRoleRequestBuilder;
import org.elasticsearch.shield.action.role.DeleteRoleResponse;
import org.elasticsearch.shield.action.role.GetRolesAction;
import org.elasticsearch.shield.action.role.GetRolesRequest;
import org.elasticsearch.shield.action.role.GetRolesRequestBuilder;
import org.elasticsearch.shield.action.role.GetRolesResponse;
import org.elasticsearch.shield.action.role.PutRoleAction;
import org.elasticsearch.shield.action.role.PutRoleRequest;
import org.elasticsearch.shield.action.role.PutRoleRequestBuilder;
import org.elasticsearch.shield.action.role.PutRoleResponse;
import org.elasticsearch.shield.action.user.ChangePasswordAction;
import org.elasticsearch.shield.action.user.ChangePasswordRequest;
import org.elasticsearch.shield.action.user.ChangePasswordRequestBuilder;
import org.elasticsearch.shield.action.user.ChangePasswordResponse;
import org.elasticsearch.shield.action.user.DeleteUserAction;
import org.elasticsearch.shield.action.user.DeleteUserRequest;
import org.elasticsearch.shield.action.user.DeleteUserRequestBuilder;
import org.elasticsearch.shield.action.user.DeleteUserResponse;
import org.elasticsearch.shield.action.user.GetUsersAction;
import org.elasticsearch.shield.action.user.GetUsersRequest;
import org.elasticsearch.shield.action.user.GetUsersRequestBuilder;
import org.elasticsearch.shield.action.user.GetUsersResponse;
import org.elasticsearch.shield.action.user.PutUserAction;
import org.elasticsearch.shield.action.user.PutUserRequest;
import org.elasticsearch.shield.action.user.PutUserRequestBuilder;
import org.elasticsearch.shield.action.user.PutUserResponse;

import java.io.IOException;

/**
 * A wrapper to elasticsearch clients that exposes all Shield related APIs
 */
public class SecurityClient {

    private final ElasticsearchClient client;

    public SecurityClient(ElasticsearchClient client) {
        this.client = client;
    }

    /****************
     * authc things *
     ****************/

    /**
     * Clears the realm caches. It's possible to clear all user entries from all realms in the cluster or alternatively
     * select the realms (by their unique names) and/or users (by their usernames) that should be evicted.
     */
    @SuppressWarnings("unchecked")
    public ClearRealmCacheRequestBuilder prepareClearRealmCache() {
        return new ClearRealmCacheRequestBuilder(client);
    }

    /**
     * Clears the realm caches. It's possible to clear all user entries from all realms in the cluster or alternatively
     * select the realms (by their unique names) and/or users (by their usernames) that should be evicted.
     */
    @SuppressWarnings("unchecked")
    public void clearRealmCache(ClearRealmCacheRequest request, ActionListener<ClearRealmCacheResponse> listener) {
        client.execute(ClearRealmCacheAction.INSTANCE, request, listener);
    }

    /**
     * Clears the realm caches. It's possible to clear all user entries from all realms in the cluster or alternatively
     * select the realms (by their unique names) and/or users (by their usernames) that should be evicted.
     */
    @SuppressWarnings("unchecked")
    public ActionFuture<ClearRealmCacheResponse> clearRealmCache(ClearRealmCacheRequest request) {
        return client.execute(ClearRealmCacheAction.INSTANCE, request);
    }

    /****************
     * authz things *
     ****************/

    /**
     * Clears the roles cache. This API only works for the naitve roles that are stored in an elasticsearch index. It is
     * possible to clear the cache of all roles or to specify the names of individual roles that should have their cache
     * cleared.
     */
    public ClearRolesCacheRequestBuilder prepareClearRolesCache() {
        return new ClearRolesCacheRequestBuilder(client);
    }

    /**
     * Clears the roles cache. This API only works for the naitve roles that are stored in an elasticsearch index. It is
     * possible to clear the cache of all roles or to specify the names of individual roles that should have their cache
     * cleared.
     */
    public void clearRolesCache(ClearRolesCacheRequest request, ActionListener<ClearRolesCacheResponse> listener) {
        client.execute(ClearRolesCacheAction.INSTANCE, request, listener);
    }

    /**
     * Clears the roles cache. This API only works for the naitve roles that are stored in an elasticsearch index. It is
     * possible to clear the cache of all roles or to specify the names of individual roles that should have their cache
     * cleared.
     */
    public ActionFuture<ClearRolesCacheResponse> clearRolesCache(ClearRolesCacheRequest request) {
        return client.execute(ClearRolesCacheAction.INSTANCE, request);
    }

    /** User Management */

    public GetUsersRequestBuilder prepareGetUsers(String... usernames) {
        return new GetUsersRequestBuilder(client).usernames(usernames);
    }

    public void getUsers(GetUsersRequest request, ActionListener<GetUsersResponse> listener) {
        client.execute(GetUsersAction.INSTANCE, request, listener);
    }

    public DeleteUserRequestBuilder prepareDeleteUser(String username) {
        return new DeleteUserRequestBuilder(client).username(username);
    }

    public void deleteUser(DeleteUserRequest request, ActionListener<DeleteUserResponse> listener) {
        client.execute(DeleteUserAction.INSTANCE, request, listener);
    }

    public PutUserRequestBuilder preparePutUser(String username, BytesReference source) throws IOException {
        return new PutUserRequestBuilder(client).source(username, source);
    }

    public PutUserRequestBuilder preparePutUser(String username, char[] password, String... roles) {
        return new PutUserRequestBuilder(client).username(username).password(password).roles(roles);
    }

    public void putUser(PutUserRequest request, ActionListener<PutUserResponse> listener) {
        client.execute(PutUserAction.INSTANCE, request, listener);
    }

    public ChangePasswordRequestBuilder prepareChangePassword(String username, char[] password) {
        return new ChangePasswordRequestBuilder(client).username(username).password(password);
    }

    public ChangePasswordRequestBuilder prepareChangePassword(String username, BytesReference source) throws IOException {
        return new ChangePasswordRequestBuilder(client).username(username).source(source);
    }

    public void changePassword(ChangePasswordRequest request, ActionListener<ChangePasswordResponse> listener) {
        client.execute(ChangePasswordAction.INSTANCE, request, listener);
    }

    /** Role Management */

    public GetRolesRequestBuilder prepareGetRoles(String... names) {
        return new GetRolesRequestBuilder(client).names(names);
    }

    public void getRoles(GetRolesRequest request, ActionListener<GetRolesResponse> listener) {
        client.execute(GetRolesAction.INSTANCE, request, listener);
    }

    public DeleteRoleRequestBuilder prepareDeleteRole(String name) {
        return new DeleteRoleRequestBuilder(client).name(name);
    }

    public void deleteRole(DeleteRoleRequest request, ActionListener<DeleteRoleResponse> listener) {
        client.execute(DeleteRoleAction.INSTANCE, request, listener);
    }

    public PutRoleRequestBuilder preparePutRole(String name) {
        return new PutRoleRequestBuilder(client).name(name);
    }

    public PutRoleRequestBuilder preparePutRole(String name, BytesReference source) throws Exception {
        return new PutRoleRequestBuilder(client).source(name, source);
    }

    public void putRole(PutRoleRequest request, ActionListener<PutRoleResponse> listener) {
        client.execute(PutRoleAction.INSTANCE, request, listener);
    }
}
