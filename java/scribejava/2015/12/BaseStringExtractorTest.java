package com.github.scribejava.core.extractors;

import static org.junit.Assert.assertEquals;
import org.junit.Before;
import org.junit.Test;
import com.github.scribejava.core.ObjectMother;
import com.github.scribejava.core.exceptions.OAuthParametersMissingException;
import com.github.scribejava.core.model.OAuthConfig;
import com.github.scribejava.core.model.OAuthRequest;
import com.github.scribejava.core.model.Verb;
import com.github.scribejava.core.oauth.OAuth20ServiceImpl;

public class BaseStringExtractorTest {

    private BaseStringExtractorImpl extractor;
    private OAuthRequest request;
    private OAuthRequest requestPort80;
    private OAuthRequest requestPort80v2;
    private OAuthRequest requestPort8080;
    private OAuthRequest requestPort443;
    private OAuthRequest requestPort443v2;

    @Before
    public void setup() {
        request = ObjectMother.createSampleOAuthRequest();
        requestPort80 = ObjectMother.createSampleOAuthRequestPort80();
        requestPort80v2 = ObjectMother.createSampleOAuthRequestPort80v2();
        requestPort8080 = ObjectMother.createSampleOAuthRequestPort8080();
        requestPort443 = ObjectMother.createSampleOAuthRequestPort443();
        requestPort443v2 = ObjectMother.createSampleOAuthRequestPort443v2();
        extractor = new BaseStringExtractorImpl();
    }

    @Test
    public void shouldExtractBaseStringFromOAuthRequest() {
        String expected
                = "GET&http%3A%2F%2Fexample.com&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(request);
        assertEquals(expected, baseString);
    }

    @Test
    public void shouldExcludePort80() {
        String expected
                = "GET&http%3A%2F%2Fexample.com&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(requestPort80);
        assertEquals(expected, baseString);
    }

    @Test
    public void shouldExcludePort80v2() {
        String expected
                = "GET&http%3A%2F%2Fexample.com%2Ftest&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(requestPort80v2);
        assertEquals(expected, baseString);
    }

    @Test
    public void shouldIncludePort8080() {
        String expected
                = "GET&http%3A%2F%2Fexample.com%3A8080&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(requestPort8080);
        assertEquals(expected, baseString);
    }

    @Test
    public void shouldExcludePort443() {
        String expected
                = "GET&https%3A%2F%2Fexample.com&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(requestPort443);
        assertEquals(expected, baseString);
    }

    @Test
    public void shouldExcludePort443v2() {
        String expected
                = "GET&https%3A%2F%2Fexample.com%2Ftest&oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        String baseString = extractor.extract(requestPort443v2);
        assertEquals(expected, baseString);
    }

    @Test(expected = IllegalArgumentException.class)
    public void shouldThrowExceptionIfRquestIsNull() {
        OAuthRequest nullRequest = null;
        extractor.extract(nullRequest);
    }

    @Test(expected = OAuthParametersMissingException.class)
    public void shouldThrowExceptionIfRquestHasNoOAuthParameters() {
        OAuthRequest request = new OAuthRequest(Verb.GET, "http://example.com", new OAuth20ServiceImpl(null, new OAuthConfig("test",
                "test")));
        extractor.extract(request);
    }

    @Test
    public void shouldProperlyEncodeSpaces() {
        String expected
                = "GET&http%3A%2F%2Fexample.com&body%3Dthis%2520param%2520has%2520whitespace%26oauth_callback%3Dhttp%253A%252F%252Fexample%252Fcallback%26oauth_consumer_key%3DAS%2523%2524%255E%252A%2540%2526%26oauth_signature%3DOAuth-Signature%26oauth_timestamp%3D123456";
        request.addBodyParameter("body", "this param has whitespace");
        assertEquals(expected, extractor.extract(request));
    }
}
