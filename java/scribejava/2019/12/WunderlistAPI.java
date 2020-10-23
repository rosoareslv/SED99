package com.github.scribejava.apis;

import java.io.OutputStream;

import com.github.scribejava.apis.wunderlist.WunderlistOAuthService;
import com.github.scribejava.core.builder.api.DefaultApi20;
import com.github.scribejava.core.httpclient.HttpClient;
import com.github.scribejava.core.httpclient.HttpClientConfig;
import com.github.scribejava.core.oauth2.bearersignature.BearerSignature;
import com.github.scribejava.core.oauth2.bearersignature.BearerSignatureURIQueryParameter;
import com.github.scribejava.core.oauth2.clientauthentication.ClientAuthentication;
import com.github.scribejava.core.oauth2.clientauthentication.RequestBodyAuthenticationScheme;

/**
 * Wunderlist.com Api
 */
public class WunderlistAPI extends DefaultApi20 {

    protected WunderlistAPI() {
    }

    private static class InstanceHolder {

        private static final WunderlistAPI INSTANCE = new WunderlistAPI();
    }

    public static WunderlistAPI instance() {
        return InstanceHolder.INSTANCE;
    }

    @Override
    public String getAccessTokenEndpoint() {
        return "https://www.wunderlist.com/oauth/access_token";
    }

    @Override
    protected String getAuthorizationBaseUrl() {
        return "https://www.wunderlist.com/oauth/authorize";
    }

    @Override
    public BearerSignature getBearerSignature() {
        return BearerSignatureURIQueryParameter.instance();
    }

    @Override
    public ClientAuthentication getClientAuthentication() {
        return RequestBodyAuthenticationScheme.instance();
    }

    @Override
    public WunderlistOAuthService createService(String apiKey, String apiSecret, String callback, String defaultScope,
            String responseType, OutputStream debugStream, String userAgent, HttpClientConfig httpClientConfig,
            HttpClient httpClient) {
        return new WunderlistOAuthService(this, apiKey, apiSecret, callback, defaultScope, responseType, debugStream,
                userAgent, httpClientConfig, httpClient);
    }
}
