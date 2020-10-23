package com.github.scribejava.apis.service;

import com.github.scribejava.apis.ImgurApi;
import com.github.scribejava.core.builder.api.DefaultApi20;
import com.github.scribejava.core.httpclient.HttpClient;
import com.github.scribejava.core.httpclient.HttpClientConfig;
import com.github.scribejava.core.model.OAuthConstants;
import com.github.scribejava.core.model.OAuthRequest;
import com.github.scribejava.core.oauth.OAuth20Service;
import java.io.OutputStream;

public class ImgurOAuthService extends OAuth20Service {

    /**
     * @deprecated use {@link #ImgurOAuthService(com.github.scribejava.core.builder.api.DefaultApi20, java.lang.String,
     * java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String,
     * com.github.scribejava.core.httpclient.HttpClientConfig, com.github.scribejava.core.httpclient.HttpClient)}
     */
    @Deprecated
    public ImgurOAuthService(DefaultApi20 api, String apiKey, String apiSecret, String callback, String scope,
            OutputStream debugStream, String state, String responseType, String userAgent,
            HttpClientConfig httpClientConfig, HttpClient httpClient) {
        this(api, apiKey, apiSecret, callback, scope, state, responseType, userAgent, httpClientConfig, httpClient);
    }

    public ImgurOAuthService(DefaultApi20 api, String apiKey, String apiSecret, String callback, String scope,
            String state, String responseType, String userAgent, HttpClientConfig httpClientConfig,
            HttpClient httpClient) {
        super(api, apiKey, apiSecret, callback, scope, state, responseType, userAgent, httpClientConfig, httpClient);
    }

    @Override
    protected OAuthRequest createAccessTokenRequest(String oauthVerifier) {
        final DefaultApi20 api = getApi();
        final OAuthRequest request = new OAuthRequest(api.getAccessTokenVerb(), api.getAccessTokenEndpoint());
        request.addBodyParameter(OAuthConstants.CLIENT_ID, getApiKey());
        request.addBodyParameter(OAuthConstants.CLIENT_SECRET, getApiSecret());

        if (ImgurApi.isOob(getCallback())) {
            request.addBodyParameter(OAuthConstants.GRANT_TYPE, "pin");
            request.addBodyParameter("pin", oauthVerifier);
        } else {
            request.addBodyParameter(OAuthConstants.GRANT_TYPE, OAuthConstants.AUTHORIZATION_CODE);
            request.addBodyParameter(OAuthConstants.CODE, oauthVerifier);
        }
        return request;
    }

    @Override
    public void signRequest(String accessToken, OAuthRequest request) {
        request.addHeader("Authorization", accessToken == null ? "Client-ID " + getApiKey() : "Bearer " + accessToken);
    }
}
