/*
 * SonarQube
 * Copyright (C) 2009-2016 SonarSource SA
 * mailto:contact AT sonarsource DOT com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.server.authentication;

import javax.servlet.http.Cookie;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.mockito.ArgumentCaptor;
import org.sonar.api.platform.Server;
import org.sonar.server.exceptions.UnauthorizedException;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class JwtCsrfVerifierTest {

  @Rule
  public ExpectedException thrown = ExpectedException.none();

  static final int TIMEOUT = 30;
  static final String CSRF_STATE = "STATE";
  static final String JAVA_WS_URL = "/api/metrics/create";

  ArgumentCaptor<Cookie> cookieArgumentCaptor = ArgumentCaptor.forClass(Cookie.class);

  Server server = mock(Server.class);
  HttpServletResponse response = mock(HttpServletResponse.class);
  HttpServletRequest request = mock(HttpServletRequest.class);

  JwtCsrfVerifier underTest = new JwtCsrfVerifier();

  @Before
  public void setUp() throws Exception {
    when(request.getContextPath()).thenReturn("");
  }

  @Test
  public void generate_state() throws Exception {
    String state = underTest.generateState(request, response, TIMEOUT);
    assertThat(state).isNotEmpty();

    verify(response).addCookie(cookieArgumentCaptor.capture());
    verifyCookie(cookieArgumentCaptor.getValue());
  }

  @Test
  public void verify_state() throws Exception {
    mockRequestCsrf(CSRF_STATE);
    mockPostJavaWsRequest();

    underTest.verifyState(request, CSRF_STATE);
  }

  @Test
  public void fail_with_unauthorized_when_state_header_is_not_the_same_as_state_parameter() throws Exception {
    mockRequestCsrf("other value");
    mockPostJavaWsRequest();

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, CSRF_STATE);
  }

  @Test
  public void fail_with_unauthorized_when_state_is_null() throws Exception {
    mockRequestCsrf(CSRF_STATE);
    mockPostJavaWsRequest();

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, null);
  }

  @Test
  public void fail_with_unauthorized_when_state_parameter_is_empty() throws Exception {
    mockRequestCsrf(CSRF_STATE);
    mockPostJavaWsRequest();

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, "");
  }

  @Test
  public void verify_POST_request() throws Exception {
    mockRequestCsrf("other value");
    when(request.getRequestURI()).thenReturn(JAVA_WS_URL);
    when(request.getMethod()).thenReturn("POST");

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, CSRF_STATE);
  }

  @Test
  public void verify_PUT_request() throws Exception {
    mockRequestCsrf("other value");
    when(request.getRequestURI()).thenReturn(JAVA_WS_URL);
    when(request.getMethod()).thenReturn("PUT");

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, CSRF_STATE);
  }

  @Test
  public void verify_DELETE_request() throws Exception {
    mockRequestCsrf("other value");
    when(request.getRequestURI()).thenReturn(JAVA_WS_URL);
    when(request.getMethod()).thenReturn("DELETE");

    thrown.expect(UnauthorizedException.class);
    underTest.verifyState(request, CSRF_STATE);
  }

  @Test
  public void ignore_GET_request() throws Exception {
    when(request.getRequestURI()).thenReturn(JAVA_WS_URL);
    when(request.getMethod()).thenReturn("GET");

    underTest.verifyState(request, null);
  }

  @Test
  public void ignore_rails_ws_requests() throws Exception {
    executeVerifyStateDoesNotFailOnRequest("/api/events", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/favourites", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/issues/add_comment?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/issues/delete_comment?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/issues/edit_comment?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/issues/bulk_change?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/projects/create?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/properties/create?key=ABCD", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/server", "POST");
    executeVerifyStateDoesNotFailOnRequest("/api/user_properties", "POST");
  }

  @Test
  public void ignore_not_api_requests() throws Exception {
    executeVerifyStateDoesNotFailOnRequest("/events", "POST");
    executeVerifyStateDoesNotFailOnRequest("/favorites", "POST");
  }

  @Test
  public void refresh_state() throws Exception {
    underTest.refreshState(request, response, CSRF_STATE, 30);

    verify(response).addCookie(cookieArgumentCaptor.capture());
    verifyCookie(cookieArgumentCaptor.getValue());
  }

  @Test
  public void remove_state() throws Exception {
    underTest.removeState(request, response);

    verify(response).addCookie(cookieArgumentCaptor.capture());
    Cookie cookie = cookieArgumentCaptor.getValue();
    assertThat(cookie.getValue()).isNull();
    assertThat(cookie.getMaxAge()).isEqualTo(0);
  }

  private void verifyCookie(Cookie cookie) {
    assertThat(cookie.getName()).isEqualTo("XSRF-TOKEN");
    assertThat(cookie.getValue()).isNotEmpty();
    assertThat(cookie.getPath()).isEqualTo("/");
    assertThat(cookie.isHttpOnly()).isFalse();
    assertThat(cookie.getMaxAge()).isEqualTo(TIMEOUT);
    assertThat(cookie.getSecure()).isFalse();
  }

  private void mockPostJavaWsRequest() {
    when(request.getRequestURI()).thenReturn(JAVA_WS_URL);
    when(request.getMethod()).thenReturn("POST");
  }

  private void mockRequestCsrf(String csrfState) {
    when(request.getHeader("X-XSRF-TOKEN")).thenReturn(csrfState);
  }

  private void executeVerifyStateDoesNotFailOnRequest(String uri, String method) {
    when(request.getRequestURI()).thenReturn(uri);
    when(request.getMethod()).thenReturn(method);

    underTest.verifyState(request, null);
  }
}
