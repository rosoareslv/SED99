package com.github.scribejava.apis;

import com.github.scribejava.core.builder.api.DefaultApi20;
import com.github.scribejava.core.model.Verb;

public class VkontakteApi extends DefaultApi20 {

    protected VkontakteApi() {
    }

    private static class InstanceHolder {
        private static final VkontakteApi INSTANCE = new VkontakteApi();
    }

    public static VkontakteApi instance() {
        return InstanceHolder.INSTANCE;
    }

    @Override
    public Verb getAccessTokenVerb() {
        return Verb.GET;
    }

    @Override
    public String getAccessTokenEndpoint() {
        return "https://oauth.vk.com/access_token";
    }

    @Override
    protected String getAuthorizationBaseUrl() {
        return "https://oauth.vk.com/authorize";
    }
}
