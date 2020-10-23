/*
 * Copyright 2002-2015 the original author or authors.
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

package org.springframework.web.reactive.method.annotation;

import java.net.URI;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CompletableFuture;

import org.junit.Test;
import org.reactivestreams.Publisher;
import reactor.Publishers;
import reactor.io.buffer.Buffer;
import reactor.rx.Promise;
import reactor.rx.Promises;
import reactor.rx.Stream;
import reactor.rx.Streams;
import rx.Observable;
import rx.Single;

import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.core.ParameterizedTypeReference;
import org.springframework.core.ResolvableType;
import org.springframework.core.codec.support.ByteBufferEncoder;
import org.springframework.core.codec.support.JacksonJsonEncoder;
import org.springframework.core.codec.support.JsonObjectEncoder;
import org.springframework.core.codec.support.StringEncoder;
import org.springframework.core.convert.ConversionService;
import org.springframework.core.convert.support.GenericConversionService;
import org.springframework.core.convert.support.ReactiveStreamsToCompletableFutureConverter;
import org.springframework.core.convert.support.ReactiveStreamsToReactorConverter;
import org.springframework.core.convert.support.ReactiveStreamsToRxJava1Converter;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.RequestEntity;
import org.springframework.http.ResponseEntity;
import org.springframework.http.server.reactive.AbstractHttpHandlerIntegrationTests;
import org.springframework.http.server.reactive.HttpHandler;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.ResponseBody;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.reactive.DispatcherHandler;
import org.springframework.web.reactive.handler.SimpleHandlerResultHandler;

import static org.junit.Assert.assertEquals;

/**
 * @author Rossen Stoyanchev
 * @author Sebastien Deleuze
 * @author Stephane Maldini
 */
public class RequestMappingIntegrationTests extends AbstractHttpHandlerIntegrationTests {

	private AnnotationConfigApplicationContext wac;


	@Override
	protected HttpHandler createHttpHandler() {
		this.wac = new AnnotationConfigApplicationContext();
		this.wac.register(FrameworkConfig.class, ApplicationConfig.class);
		this.wac.refresh();

		DispatcherHandler dispatcherHandler = new DispatcherHandler();
		dispatcherHandler.setApplicationContext(this.wac);
		return dispatcherHandler;
	}

	@Test
	public void helloWithQueryParam() throws Exception {

		RestTemplate restTemplate = new RestTemplate();

		URI url = new URI("http://localhost:" + port + "/param?name=George");
		RequestEntity<Void> request = RequestEntity.get(url).build();
		ResponseEntity<String> response = restTemplate.exchange(request, String.class);

		assertEquals("Hello George!", response.getBody());
	}

	@Test
	public void rawPojoResponse() throws Exception {

		RestTemplate restTemplate = new RestTemplate();

		URI url = new URI("http://localhost:" + port + "/raw");
		RequestEntity<Void> request = RequestEntity.get(url).build();
		Person person = restTemplate.exchange(request, Person.class).getBody();

		assertEquals(new Person("Robert"), person);
	}

	@Test
	public void rawHelloResponse() throws Exception {

		RestTemplate restTemplate = new RestTemplate();

		URI url = new URI("http://localhost:" + port + "/raw-observable");
		RequestEntity<Void> request = RequestEntity.get(url).build();
		ResponseEntity<String> response = restTemplate.exchange(request, String.class);

		assertEquals("Hello!", response.getBody());
	}

	@Test
	public void handleWithThrownException() throws Exception {

		RestTemplate restTemplate = new RestTemplate();

		URI url = new URI("http://localhost:" + port + "/thrown-exception");
		RequestEntity<Void> request = RequestEntity.get(url).build();
		ResponseEntity<String> response = restTemplate.exchange(request, String.class);

		assertEquals("Recovered from error: Boo", response.getBody());
	}

	@Test
	public void handleWithErrorSignal() throws Exception {

		RestTemplate restTemplate = new RestTemplate();

		URI url = new URI("http://localhost:" + port + "/error-signal");
		RequestEntity<Void> request = RequestEntity.get(url).build();
		ResponseEntity<String> response = restTemplate.exchange(request, String.class);

		assertEquals("Recovered from error: Boo", response.getBody());
	}

	@Test
	public void serializeAsPojo() throws Exception {
		serializeAsPojo("http://localhost:" + port + "/person");
	}

	@Test
	public void serializeAsCompletableFuture() throws Exception {
		serializeAsPojo("http://localhost:" + port + "/completable-future");
	}

	@Test
	public void serializeAsSingle() throws Exception {
		serializeAsPojo("http://localhost:" + port + "/single");
	}

	@Test
	public void serializeAsPromise() throws Exception {
		serializeAsPojo("http://localhost:" + port + "/promise");
	}

	@Test
	public void serializeAsList() throws Exception {
		serializeAsCollection("http://localhost:" + port + "/list");
	}

	@Test
	public void serializeAsPublisher() throws Exception {
		serializeAsCollection("http://localhost:" + port + "/publisher");
	}

	@Test
	public void serializeAsObservable() throws Exception {
		serializeAsCollection("http://localhost:" + port + "/observable");
	}

	@Test
	public void serializeAsReactorStream() throws Exception {
		serializeAsCollection("http://localhost:" + port + "/stream");
	}

	@Test
	public void publisherCapitalize() throws Exception {
		capitalizeCollection("http://localhost:" + port + "/publisher-capitalize");
	}

	@Test
	public void observableCapitalize() throws Exception {
		capitalizeCollection("http://localhost:" + port + "/observable-capitalize");
	}

	@Test
	public void streamCapitalize() throws Exception {
		capitalizeCollection("http://localhost:" + port + "/stream-capitalize");
	}

	@Test
	public void personCapitalize() throws Exception {
		capitalizePojo("http://localhost:" + port + "/person-capitalize");
	}
	
	@Test
	public void completableFutureCapitalize() throws Exception {
		capitalizePojo("http://localhost:" + port + "/completable-future-capitalize");
	}

	@Test
	public void singleCapitalize() throws Exception {
		capitalizePojo("http://localhost:" + port + "/single-capitalize");
	}

	@Test
	public void promiseCapitalize() throws Exception {
		capitalizePojo("http://localhost:" + this.port + "/promise-capitalize");
	}

	@Test
	public void publisherCreate() throws Exception {
		create("http://localhost:" + this.port + "/publisher-create");
	}

	@Test
	public void streamCreate() throws Exception {
		create("http://localhost:" + this.port + "/stream-create");
	}

	@Test
	public void observableCreate() throws Exception {
		create("http://localhost:" + this.port + "/observable-create");
	}


	private void serializeAsPojo(String requestUrl) throws Exception {
		RestTemplate restTemplate = new RestTemplate();
		RequestEntity<Void> request = RequestEntity.get(new URI(requestUrl))
				.accept(MediaType.APPLICATION_JSON)
				.build();
		ResponseEntity<Person> response = restTemplate.exchange(request, Person.class);

		assertEquals(new Person("Robert"), response.getBody());
	}

	private void serializeAsCollection(String requestUrl) throws Exception {
		RestTemplate restTemplate = new RestTemplate();
		RequestEntity<Void> request = RequestEntity.get(new URI(requestUrl))
				.accept(MediaType.APPLICATION_JSON)
				.build();
		List<Person> results = restTemplate.exchange(request,
				new ParameterizedTypeReference<List<Person>>(){}).getBody();

		assertEquals(2, results.size());
		assertEquals(new Person("Robert"), results.get(0));
		assertEquals(new Person("Marie"), results.get(1));
	}


	private void capitalizePojo(String requestUrl) throws Exception {
		RestTemplate restTemplate = new RestTemplate();
		RequestEntity<Person> request = RequestEntity.post(new URI(requestUrl))
				.contentType(MediaType.APPLICATION_JSON)
				.accept(MediaType.APPLICATION_JSON)
				.body(new Person("Robert"));
		ResponseEntity<Person> response = restTemplate.exchange(request, Person.class);

		assertEquals(new Person("ROBERT"), response.getBody());
	}

	private void capitalizeCollection(String requestUrl) throws Exception {
		RestTemplate restTemplate = new RestTemplate();
		RequestEntity<List<Person>> request = RequestEntity.post(new URI(requestUrl))
				.contentType(MediaType.APPLICATION_JSON)
				.accept(MediaType.APPLICATION_JSON)
				.body(Arrays.asList(new Person("Robert"), new Person("Marie")));
		List<Person> results = restTemplate.exchange(request,
				new ParameterizedTypeReference<List<Person>>(){}).getBody();

		assertEquals(2, results.size());
		assertEquals("ROBERT", results.get(0).getName());
		assertEquals("MARIE", results.get(1).getName());
	}

	private void create(String requestUrl) throws Exception {
		RestTemplate restTemplate = new RestTemplate();
		URI url = new URI(requestUrl);
		RequestEntity<List<Person>> request = RequestEntity.post(url)
				.contentType(MediaType.APPLICATION_JSON)
				.body(Arrays.asList(new Person("Robert"), new Person("Marie")));
		ResponseEntity<Void> response = restTemplate.exchange(request, Void.class);

		assertEquals(HttpStatus.OK, response.getStatusCode());
		assertEquals(2, this.wac.getBean(TestController.class).persons.size());
	}


	@Configuration
	@SuppressWarnings("unused")
	static class FrameworkConfig {

		@Bean
		public RequestMappingHandlerMapping handlerMapping() {
			return new RequestMappingHandlerMapping();
		}

		@Bean
		public RequestMappingHandlerAdapter handlerAdapter() {
			RequestMappingHandlerAdapter handlerAdapter = new RequestMappingHandlerAdapter();
			handlerAdapter.setConversionService(conversionService());
			return handlerAdapter;
		}

		@Bean
		public ConversionService conversionService() {
			// TODO: test failures with DefaultConversionService
			GenericConversionService service = new GenericConversionService();
			service.addConverter(new ReactiveStreamsToCompletableFutureConverter());
			service.addConverter(new ReactiveStreamsToReactorConverter());
			service.addConverter(new ReactiveStreamsToRxJava1Converter());
			return service;
		}

		@Bean
		public ResponseBodyResultHandler responseBodyResultHandler() {
			return new ResponseBodyResultHandler(Arrays.asList(
					new ByteBufferEncoder(), new StringEncoder(), new JacksonJsonEncoder(new JsonObjectEncoder())),
					conversionService());
		}

		@Bean
		public SimpleHandlerResultHandler simpleHandlerResultHandler() {
			return new SimpleHandlerResultHandler(conversionService());
		}

	}

	@Configuration
	@SuppressWarnings("unused")
	static class ApplicationConfig {

		@Bean
		public TestController testController() {
			return new TestController();
		}
	}


	@Controller
	@SuppressWarnings("unused")
	private static class TestController {

		final List<Person> persons = new ArrayList<>();

		@RequestMapping("/param")
		@ResponseBody
		public Publisher<String> handleWithParam(@RequestParam String name) {
			return Streams.just("Hello ", name, "!");
		}

		@RequestMapping("/person")
		@ResponseBody
		public Person personResponseBody() {
			return new Person("Robert");
		}

		@RequestMapping("/completable-future")
		@ResponseBody
		public CompletableFuture<Person> completableFutureResponseBody() {
			return CompletableFuture.completedFuture(new Person("Robert"));
		}

		@RequestMapping("/raw")
		@ResponseBody
		public Publisher<ByteBuffer> rawResponseBody() {
			JacksonJsonEncoder encoder = new JacksonJsonEncoder();
			return encoder.encode(Streams.just(new Person("Robert")),
					ResolvableType.forClass(Person.class), MediaType.APPLICATION_JSON);
		}

		@RequestMapping("/raw-observable")
		@ResponseBody
		public Observable<ByteBuffer> rawObservableResponseBody() {
			return Observable.just(Buffer.wrap("Hello!").byteBuffer());
		}

		@RequestMapping("/single")
		@ResponseBody
		public Single<Person> singleResponseBody() {
			return Single.just(new Person("Robert"));
		}

		@RequestMapping("/promise")
		@ResponseBody
		public Promise<Person> promiseResponseBody() {
			return Promises.success(new Person("Robert"));
		}

		@RequestMapping("/list")
		@ResponseBody
		public List<Person> listResponseBody() {
			return Arrays.asList(new Person("Robert"), new Person("Marie"));
		}

		@RequestMapping("/publisher")
		@ResponseBody
		public Publisher<Person> publisherResponseBody() {
			return Streams.just(new Person("Robert"), new Person("Marie"));
		}

		@RequestMapping("/observable")
		@ResponseBody
		public Observable<Person> observableResponseBody() {
			return Observable.just(new Person("Robert"), new Person("Marie"));
		}

		@RequestMapping("/stream")
		@ResponseBody
		public Stream<Person> reactorStreamResponseBody() {
			return Streams.just(new Person("Robert"), new Person("Marie"));
		}

		@RequestMapping("/publisher-capitalize")
		@ResponseBody
		public Publisher<Person> publisherCapitalize(@RequestBody Publisher<Person> persons) {
			return Streams.wrap(persons).map(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/observable-capitalize")
		@ResponseBody
		public Observable<Person> observableCapitalize(@RequestBody Observable<Person> persons) {
			return persons.map(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/stream-capitalize")
		@ResponseBody
		public Stream<Person> streamCapitalize(@RequestBody Stream<Person> persons) {
			return persons.map(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/person-capitalize")
		@ResponseBody
		public Person personCapitalize(@RequestBody Person person) {
			person.setName(person.getName().toUpperCase());
			return person;
		}
		
		@RequestMapping("/completable-future-capitalize")
		@ResponseBody
		public CompletableFuture<Person> completableFutureCapitalize(
				@RequestBody CompletableFuture<Person> personFuture) {

			return personFuture.thenApply(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/single-capitalize")
		@ResponseBody
		public Single<Person> singleCapitalize(@RequestBody Single<Person> personFuture) {
			return personFuture.map(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/promise-capitalize")
		@ResponseBody
		public Promise<Person> promiseCapitalize(@RequestBody Promise<Person> personFuture) {
			return personFuture.map(person -> {
				person.setName(person.getName().toUpperCase());
				return person;
			});
		}

		@RequestMapping("/publisher-create")
		public Publisher<Void> publisherCreate(@RequestBody Publisher<Person> personStream) {
			return Streams.wrap(personStream).toList().onSuccess(persons::addAll).after();
		}

		@RequestMapping("/stream-create")
		public Promise<Void> streamCreate(@RequestBody Stream<Person> personStream) {
			return personStream.toList().onSuccess(persons::addAll).after();
		}

		@RequestMapping("/observable-create")
		public Observable<Void> observableCreate(@RequestBody Observable<Person> personStream) {
			return personStream.toList().doOnNext(persons::addAll).flatMap(document -> Observable.empty());
		}

		@RequestMapping("/thrown-exception")
		@ResponseBody
		public Publisher<String> handleAndThrowException() {
			throw new IllegalStateException("Boo");
		}

		@RequestMapping("/error-signal")
		@ResponseBody
		public Publisher<String> handleWithError() {
			return Publishers.error(new IllegalStateException("Boo"));
		}

		@ExceptionHandler
		@ResponseBody
		public Publisher<String> handleException(IllegalStateException ex) {
			return Streams.just("Recovered from error: " + ex.getMessage());
		}

		//TODO add mixed and T request mappings tests

	}

	private static class Person {

		private String name;

		@SuppressWarnings("unused")
		public Person() {
		}

		public Person(String name) {
			this.name = name;
		}

		public String getName() {
			return name;
		}

		public void setName(String name) {
			this.name = name;
		}

		@Override
		public boolean equals(Object o) {
			if (this == o) {
				return true;
			}
			if (o == null || getClass() != o.getClass()) {
				return false;
			}
			Person person = (Person) o;
			return !(this.name != null ? !this.name.equals(person.name) : person.name != null);
		}

		@Override
		public int hashCode() {
			return this.name != null ? this.name.hashCode() : 0;
		}
	}

}
