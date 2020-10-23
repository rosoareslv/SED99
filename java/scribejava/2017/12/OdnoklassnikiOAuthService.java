package com.github.scribejava.apis.service;

import com.github.scribejava.core.builder.api.DefaultApi20;
import com.github.scribejava.core.model.OAuthConfig;
import com.github.scribejava.core.model.OAuthRequest;
import com.github.scribejava.core.model.Parameter;
import com.github.scribejava.core.model.ParameterList;
import com.github.scribejava.core.oauth.OAuth20Service;
import java.io.UnsupportedEncodingException;

import java.net.URLDecoder;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Collections;
import java.util.Formatter;
import java.util.List;
import java.util.stream.Collectors;

public class OdnoklassnikiOAuthService extends OAuth20Service {

    public OdnoklassnikiOAuthService(DefaultApi20 api, OAuthConfig config) {
        super(api, config);
    }

    @Override
    public void signRequest(String accessToken, OAuthRequest request) {
        //sig = lower(md5( sorted_request_params_composed_string + md5(access_token + application_secret_key)))
        try {
            final String tokenDigest = md5(accessToken + getConfig().getApiSecret());

            final ParameterList queryParams = request.getQueryStringParams();
            queryParams.addAll(request.getBodyParams());
            final List<Parameter> allParams = queryParams.getParams();

            Collections.sort(allParams);

            final String stringParams = allParams.stream()
                    .map(param -> param.getKey() + '=' + param.getValue())
                    .collect(Collectors.joining());

            final String sigSource = URLDecoder.decode(stringParams, "UTF-8") + tokenDigest;
            request.addQuerystringParameter("sig", md5(sigSource).toLowerCase());

            super.signRequest(accessToken, request);
        } catch (UnsupportedEncodingException unex) {
            throw new IllegalStateException(unex);
        }
    }

    public static String md5(String orgString) {
        try {
            final MessageDigest md = MessageDigest.getInstance("MD5");
            final byte[] array = md.digest(orgString.getBytes(Charset.forName("UTF-8")));
            final Formatter builder = new Formatter();
            for (byte b : array) {
                builder.format("%02x", b);
            }
            return builder.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("MD5 is unsupported?", e);
        }
    }
}
