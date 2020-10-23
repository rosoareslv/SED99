/*
 * SonarQube
 * Copyright (C) 2009-2017 SonarSource SA
 * mailto:info AT sonarsource DOT com
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
package org.sonar.scanner;

import java.io.InputStream;
import java.io.Reader;
import org.apache.commons.lang.StringUtils;
import org.hamcrest.BaseMatcher;
import org.hamcrest.Description;
import org.sonar.scanner.bootstrap.ScannerWsClient;
import org.sonarqube.ws.client.WsRequest;
import org.sonarqube.ws.client.WsResponse;

import static org.mockito.Matchers.any;
import static org.mockito.Matchers.argThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class WsTestUtil {
  public static void mockStream(ScannerWsClient mock, String path, InputStream is) {
    WsResponse response = mock(WsResponse.class);
    when(response.contentStream()).thenReturn(is);
    when(mock.call(argThat(new RequestMatcher(path)))).thenReturn(response);
  }

  public static void mockStream(ScannerWsClient mock, InputStream is) {
    WsResponse response = mock(WsResponse.class);
    when(response.contentStream()).thenReturn(is);
    when(mock.call(any(WsRequest.class))).thenReturn(response);
  }

  public static void mockReader(ScannerWsClient mock, Reader reader) {
    WsResponse response = mock(WsResponse.class);
    when(response.contentReader()).thenReturn(reader);
    when(mock.call(any(WsRequest.class))).thenReturn(response);
  }

  public static void mockReader(ScannerWsClient mock, String path, Reader reader) {
    WsResponse response = mock(WsResponse.class);
    when(response.contentReader()).thenReturn(reader);
    when(mock.call(argThat(new RequestMatcher(path)))).thenReturn(response);
  }

  public static void mockException(ScannerWsClient mock, Exception e) {
    when(mock.call(any(WsRequest.class))).thenThrow(e);
  }

  public static void mockException(ScannerWsClient mock, String path, Exception e) {
    when(mock.call(argThat(new RequestMatcher(path)))).thenThrow(e);
  }

  public static void verifyCall(ScannerWsClient mock, String path) {
    verify(mock).call(argThat(new RequestMatcher(path)));
  }

  private static class RequestMatcher extends BaseMatcher<WsRequest> {
    private String path;

    public RequestMatcher(String path) {
      this.path = path;
    }

    @Override
    public boolean matches(Object item) {
      if (item == null) {
        return false;
      }
      WsRequest request = (WsRequest) item;
      return StringUtils.equals(request.getPath(), path);
    }

    @Override
    public void describeTo(Description description) {
      description.appendText("request path (\"" + path + "\")");
    }
  }
}
