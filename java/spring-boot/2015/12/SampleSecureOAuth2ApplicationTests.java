package sample.secure.oauth2;

import java.util.Map;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.SpringApplicationConfiguration;
import org.springframework.boot.test.WebIntegrationTest;
import org.springframework.hateoas.MediaTypes;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.crypto.codec.Base64;
import org.springframework.security.web.FilterChainProxy;
import org.springframework.test.context.junit4.SpringJUnit4ClassRunner;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;
import org.springframework.web.context.WebApplicationContext;

import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultHandlers.print;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;
import static org.springframework.test.web.servlet.setup.MockMvcBuilders.webAppContextSetup;

/**
 * Series of automated integration tests to verify proper behavior of auto-configured,
 * OAuth2-secured system
 *
 * @author Greg Turnquist
 */
@RunWith(SpringJUnit4ClassRunner.class)
@SpringApplicationConfiguration(SampleSecureOAuth2Application.class)
@WebIntegrationTest(randomPort = true)
public class SampleSecureOAuth2ApplicationTests {

	@Autowired
	WebApplicationContext context;

	@Autowired
	FilterChainProxy filterChain;

	private MockMvc mvc;

	private final ObjectMapper objectMapper = new ObjectMapper();

	@Before
	public void setUp() {
		this.mvc = webAppContextSetup(this.context).addFilters(this.filterChain).build();
		SecurityContextHolder.clearContext();
	}

	@Test
	public void everythingIsSecuredByDefault() throws Exception {
		this.mvc.perform(get("/").accept(MediaTypes.HAL_JSON))
				.andExpect(status().isUnauthorized()).andDo(print());
		this.mvc.perform(get("/flights").accept(MediaTypes.HAL_JSON))
				.andExpect(status().isUnauthorized()).andDo(print());
		this.mvc.perform(get("/flights/1").accept(MediaTypes.HAL_JSON))
				.andExpect(status().isUnauthorized()).andDo(print());
		this.mvc.perform(get("/alps").accept(MediaTypes.HAL_JSON))
				.andExpect(status().isUnauthorized()).andDo(print());
	}

	@Test
	@Ignore
	public void accessingRootUriPossibleWithUserAccount() throws Exception {
		String header = "Basic " + new String(Base64.encode("greg:turnquist".getBytes()));
		this.mvc.perform(
				get("/").accept(MediaTypes.HAL_JSON).header("Authorization", header))
				.andExpect(
						header().string("Content-Type", MediaTypes.HAL_JSON.toString()))
				.andExpect(status().isOk()).andDo(print());
	}

	@Test
	public void useAppSecretsPlusUserAccountToGetBearerToken() throws Exception {
		String header = "Basic " + new String(Base64.encode("foo:bar".getBytes()));
		MvcResult result = this.mvc
				.perform(post("/oauth/token").header("Authorization", header)
						.param("grant_type", "password").param("scope", "read")
						.param("username", "greg").param("password", "turnquist"))
				.andExpect(status().isOk()).andDo(print()).andReturn();
		Object accessToken = this.objectMapper
				.readValue(result.getResponse().getContentAsString(), Map.class)
				.get("access_token");
		MvcResult flightsAction = this.mvc
				.perform(get("/flights/1").accept(MediaTypes.HAL_JSON)
						.header("Authorization", "Bearer " + accessToken))
				.andExpect(
						header().string("Content-Type", MediaTypes.HAL_JSON.toString()))
				.andExpect(status().isOk()).andDo(print()).andReturn();

		Flight flight = this.objectMapper.readValue(
				flightsAction.getResponse().getContentAsString(), Flight.class);

		assertThat(flight.getOrigin(), is("Nashville"));
		assertThat(flight.getDestination(), is("Dallas"));
		assertThat(flight.getAirline(), is("Spring Ways"));
		assertThat(flight.getFlightNumber(), is("OAUTH2"));
		assertThat(flight.getTraveler(), is("Greg Turnquist"));
	}

}
