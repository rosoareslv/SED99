package com.github.scribejava.core.model;

import com.github.scribejava.core.exceptions.OAuthException;

import java.net.URI;

/**
 * Representing <a href="https://tools.ietf.org/html/rfc6749#section-5.2">"5.2. Error Response"</a>
 */
public class OAuth2AccessTokenErrorResponse extends OAuthException {

    private static final long serialVersionUID = 2309424849700276816L;

    public enum ErrorCode {
        invalid_request, invalid_client, invalid_grant, unauthorized_client, unsupported_grant_type, invalid_scope
    }

    private final ErrorCode errorCode;
    private final String errorDescription;
    private final URI errorUri;
    private final String rawResponse;

    public OAuth2AccessTokenErrorResponse(ErrorCode errorCode, String errorDescription, URI errorUri,
            String rawResponse) {
        super(rawResponse);
        if (errorCode == null) {
            throw new IllegalArgumentException("errorCode must not be null");
        }
        this.errorCode = errorCode;
        this.errorDescription = errorDescription;
        this.errorUri = errorUri;
        this.rawResponse = rawResponse;
    }

    public ErrorCode getErrorCode() {
        return errorCode;
    }

    public String getErrorDescription() {
        return errorDescription;
    }

    public URI getErrorUri() {
        return errorUri;
    }

    public String getRawResponse() {
        return rawResponse;
    }
}
