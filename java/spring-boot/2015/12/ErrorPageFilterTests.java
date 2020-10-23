/*
 * Copyright 2012-2015 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.context.web;

import java.io.IOException;

import javax.servlet.RequestDispatcher;
import javax.servlet.ServletException;
import javax.servlet.ServletRequest;
import javax.servlet.ServletResponse;
import javax.servlet.http.HttpServletResponse;
import javax.servlet.http.HttpServletResponseWrapper;

import org.junit.Rule;
import org.junit.Test;

import org.springframework.boot.context.embedded.ErrorPage;
import org.springframework.boot.test.OutputCapture;
import org.springframework.http.HttpStatus;
import org.springframework.mock.web.MockFilterChain;
import org.springframework.mock.web.MockFilterConfig;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.web.context.request.async.DeferredResult;
import org.springframework.web.context.request.async.StandardServletAsyncWebRequest;
import org.springframework.web.context.request.async.WebAsyncManager;
import org.springframework.web.context.request.async.WebAsyncUtils;
import org.springframework.web.util.NestedServletException;

import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

/**
 * Tests for {@link ErrorPageFilter}.
 *
 * @author Dave Syer
 * @author Andy Wilkinson
 */
public class ErrorPageFilterTests {

	private ErrorPageFilter filter = new ErrorPageFilter();

	private MockHttpServletRequest request = new MockHttpServletRequest("GET",
			"/test/path");

	private MockHttpServletResponse response = new MockHttpServletResponse();

	private MockFilterChain chain = new MockFilterChain();

	@Rule
	public OutputCapture output = new OutputCapture();

	@Test
	public void notAnError() throws Exception {
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), is(nullValue()));
	}

	@Test
	public void notAnErrorButNotOK() throws Exception {
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).setStatus(201);
				super.doFilter(request, response);
				response.flushBuffer();
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponse) this.chain.getResponse()).getStatus(),
				equalTo(201));
		assertThat(((HttpServletResponse) ((HttpServletResponseWrapper) this.chain
				.getResponse()).getResponse()).getStatus(), equalTo(201));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void unauthorizedWithErrorPath() throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(401, "UNAUTHORIZED");
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		HttpServletResponseWrapper wrapper = (HttpServletResponseWrapper) this.chain
				.getResponse();
		assertThat(wrapper.getResponse(), equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
		assertThat(wrapper.getStatus(), equalTo(401));
		// The real response has to be 401 as well...
		assertThat(this.response.getStatus(), equalTo(401));
		assertThat(this.response.getForwardedUrl(), equalTo("/error"));
	}

	@Test
	public void responseCommitted() throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.response.setCommitted(true);
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(400));
		assertThat(this.response.getForwardedUrl(), is(nullValue()));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseUncommittedWithoutErrorPage() throws Exception {
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(400));
		assertThat(this.response.getForwardedUrl(), is(nullValue()));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void oncePerRequest() throws Exception {
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				assertNotNull(request.getAttribute("FILTER.FILTERED"));
				super.doFilter(request, response);
			}
		};
		this.filter.init(new MockFilterConfig("FILTER"));
		this.filter.doFilter(this.request, this.response, this.chain);
	}

	@Test
	public void globalError() throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(400));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE),
				equalTo((Object) 400));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_MESSAGE),
				equalTo((Object) "BAD"));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI),
				equalTo((Object) "/test/path"));

		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), equalTo("/error"));
	}

	@Test
	public void statusError() throws Exception {
		this.filter.addErrorPages(new ErrorPage(HttpStatus.BAD_REQUEST, "/400"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(400));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE),
				equalTo((Object) 400));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_MESSAGE),
				equalTo((Object) "BAD"));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI),
				equalTo((Object) "/test/path"));
		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), equalTo("/400"));
	}

	@Test
	public void statusErrorWithCommittedResponse() throws Exception {
		this.filter.addErrorPages(new ErrorPage(HttpStatus.BAD_REQUEST, "/400"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				((HttpServletResponse) response).sendError(400, "BAD");
				response.flushBuffer();
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(400));
		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), is(nullValue()));
	}

	@Test
	public void exceptionError() throws Exception {
		this.filter.addErrorPages(new ErrorPage(RuntimeException.class, "/500"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new RuntimeException("BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE),
				equalTo((Object) 500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_MESSAGE),
				equalTo((Object) "BAD"));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_EXCEPTION_TYPE),
				equalTo((Object) RuntimeException.class.getName()));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI),
				equalTo((Object) "/test/path"));
		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), equalTo("/500"));
	}

	@Test
	public void exceptionErrorWithCommittedResponse() throws Exception {
		this.filter.addErrorPages(new ErrorPage(RuntimeException.class, "/500"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				response.flushBuffer();
				throw new RuntimeException("BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.response.getForwardedUrl(), is(nullValue()));
	}

	@Test
	public void statusCode() throws Exception {
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				assertThat(((HttpServletResponse) response).getStatus(), equalTo(200));
				super.doFilter(request, response);
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(200));
	}

	@Test
	public void subClassExceptionError() throws Exception {
		this.filter.addErrorPages(new ErrorPage(RuntimeException.class, "/500"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new IllegalStateException("BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE),
				equalTo((Object) 500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_MESSAGE),
				equalTo((Object) "BAD"));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_EXCEPTION_TYPE),
				equalTo((Object) IllegalStateException.class.getName()));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI),
				equalTo((Object) "/test/path"));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseIsNotCommittedWhenRequestIsAsync() throws Exception {
		this.request.setAsyncStarted(true);
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertFalse(this.response.isCommitted());
	}

	@Test
	public void responseIsCommittedWhenRequestIsAsyncAndExceptionIsThrown()
			throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.request.setAsyncStarted(true);
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new RuntimeException("BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseIsCommittedWhenRequestIsAsyncAndStatusIs400Plus()
			throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.request.setAsyncStarted(true);
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				((HttpServletResponse) response).sendError(400, "BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseIsNotCommittedDuringAsyncDispatch() throws Exception {
		setUpAsyncDispatch();
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertFalse(this.response.isCommitted());
	}

	@Test
	public void responseIsCommittedWhenExceptionIsThrownDuringAsyncDispatch()
			throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		setUpAsyncDispatch();
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new RuntimeException("BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseIsCommittedWhenStatusIs400PlusDuringAsyncDispatch()
			throws Exception {
		this.filter.addErrorPages(new ErrorPage("/error"));
		setUpAsyncDispatch();
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				((HttpServletResponse) response).sendError(400, "BAD");
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.chain.getRequest(), equalTo((ServletRequest) this.request));
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getResponse(),
				equalTo((ServletResponse) this.response));
		assertTrue(this.response.isCommitted());
	}

	@Test
	public void responseIsNotFlushedIfStatusIsLessThan400AndItHasAlreadyBeenCommitted()
			throws Exception {
		HttpServletResponse committedResponse = mock(HttpServletResponse.class);
		given(committedResponse.isCommitted()).willReturn(true);
		given(committedResponse.getStatus()).willReturn(200);
		this.filter.doFilter(this.request, committedResponse, this.chain);
		verify(committedResponse, times(0)).flushBuffer();
	}

	@Test
	public void errorMessageForRequestWithoutPathInfo()
			throws IOException, ServletException {
		this.request.setServletPath("/test");
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new RuntimeException();
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.output.toString(), containsString("request [/test]"));
	}

	@Test
	public void errorMessageForRequestWithPathInfo()
			throws IOException, ServletException {
		this.request.setServletPath("/test");
		this.request.setPathInfo("/alpha");
		this.filter.addErrorPages(new ErrorPage("/error"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new RuntimeException();
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(this.output.toString(), containsString("request [/test/alpha]"));
	}

	@Test
	public void nestedServletExceptionIsUnwrapped() throws Exception {
		this.filter.addErrorPages(new ErrorPage(RuntimeException.class, "/500"));
		this.chain = new MockFilterChain() {
			@Override
			public void doFilter(ServletRequest request, ServletResponse response)
					throws IOException, ServletException {
				super.doFilter(request, response);
				throw new NestedServletException("Wrapper", new RuntimeException("BAD"));
			}
		};
		this.filter.doFilter(this.request, this.response, this.chain);
		assertThat(((HttpServletResponseWrapper) this.chain.getResponse()).getStatus(),
				equalTo(500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE),
				equalTo((Object) 500));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_MESSAGE),
				equalTo((Object) "BAD"));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_EXCEPTION_TYPE),
				equalTo((Object) RuntimeException.class.getName()));
		assertThat(this.request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI),
				equalTo((Object) "/test/path"));
		assertTrue(this.response.isCommitted());
		assertThat(this.response.getForwardedUrl(), equalTo("/500"));
	}

	private void setUpAsyncDispatch() throws Exception {
		this.request.setAsyncSupported(true);
		this.request.setAsyncStarted(true);
		DeferredResult<String> result = new DeferredResult<String>();
		WebAsyncManager asyncManager = WebAsyncUtils.getAsyncManager(this.request);
		asyncManager.setAsyncWebRequest(
				new StandardServletAsyncWebRequest(this.request, this.response));
		asyncManager.startDeferredResultProcessing(result);
	}

}
