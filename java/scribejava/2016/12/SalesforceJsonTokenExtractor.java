package com.github.scribejava.apis.salesforce;

import com.github.scribejava.core.extractors.OAuth2AccessTokenJsonExtractor;

/**
 * This extractor parses in addition to the standard Extractor the instance_url
 * of the used Salesforce organization.
 */
public class SalesforceJsonTokenExtractor extends OAuth2AccessTokenJsonExtractor {

    private static final String INSTANCE_URL_REGEX = "\"instance_url\"\\s*:\\s*\"(\\S*?)\"";

    protected SalesforceJsonTokenExtractor() {
    }

    private static class InstanceHolder {

        private static final SalesforceJsonTokenExtractor INSTANCE = new SalesforceJsonTokenExtractor();
    }

    public static SalesforceJsonTokenExtractor instance() {
        return InstanceHolder.INSTANCE;
    }

    @Override
    protected SalesforceToken createToken(String accessToken, String tokenType, Integer expiresIn,
            String refreshToken, String scope, String response) {
        return new SalesforceToken(accessToken, tokenType, expiresIn, refreshToken, scope,
                extractParameter(response, INSTANCE_URL_REGEX, true), response);
    }
}
