package com.alibaba.fastjson.support.spring;

import java.io.ByteArrayOutputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.springframework.http.MediaType;
import org.springframework.util.CollectionUtils;
import org.springframework.validation.BindingResult;
import org.springframework.web.servlet.view.AbstractView;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.serializer.SerializeFilter;
import com.alibaba.fastjson.serializer.SerializerFeature;
import com.alibaba.fastjson.support.config.FastJsonConfig;
import com.alibaba.fastjson.util.IOUtils;

/**
 * Fastjson for Spring MVC View.
 *
 * @author libinsong1204@gmail.com
 * @author VictorZeng
 * 
 * @since 1.2.9
 * @see AbstractView
 */

public class FastJsonJsonView extends AbstractView {

	/** default content type */
	public static final String DEFAULT_CONTENT_TYPE = MediaType.APPLICATION_JSON_UTF8_VALUE;

	@Deprecated
	protected Charset charset = IOUtils.UTF8;

	@Deprecated
	protected SerializerFeature[] features = new SerializerFeature[0];

	@Deprecated
	protected SerializeFilter[] filters = new SerializeFilter[0];

	@Deprecated
	protected String dateFormat;
	
	/** renderedAttributes */
	private Set<String> renderedAttributes;

	/** disableCaching */
	private boolean disableCaching = true;

	/** updateContentLength */
	private boolean updateContentLength = false;

	/** extractValueFromSingleKeyModel */
	private boolean extractValueFromSingleKeyModel = false;

	/** with fastJson config */
	private FastJsonConfig fastJsonConfig = new FastJsonConfig(); 

	/**
	 * Set default param.
	 */
	public FastJsonJsonView() {

		setContentType(DEFAULT_CONTENT_TYPE);
		setExposePathVariables(false);
	}

	/**
	 * @since 1.2.11
	 * 
	 * @return the fastJsonConfig.
	 */
	public FastJsonConfig getFastJsonConfig() {
		return fastJsonConfig;
	}

	/**
	 * @since 1.2.11
	 * 
	 * @param fastJsonConfig the fastJsonConfig to set.
	 */
	public void setFastJsonConfig(FastJsonConfig fastJsonConfig) {
		this.fastJsonConfig = fastJsonConfig;
	}
	
	@Deprecated
	public void setSerializerFeature(SerializerFeature... features) {
		this.fastJsonConfig.setSerializerFeatures(features);
	}
	
	@Deprecated
	public Charset getCharset() {
		return this.fastJsonConfig.getCharset();
	}

	@Deprecated
	public void setCharset(Charset charset) {
		this.fastJsonConfig.setCharset(charset);
	}

	@Deprecated
	public String getDateFormat() {
		return this.fastJsonConfig.getDateFormat();
	}

	@Deprecated
	public void setDateFormat(String dateFormat) {
		this.fastJsonConfig.setDateFormat(dateFormat);
	}
	
	@Deprecated
	public SerializerFeature[] getFeatures() {
		return this.fastJsonConfig.getSerializerFeatures();
	}

	@Deprecated
	public void setFeatures(SerializerFeature... features) {
		this.fastJsonConfig.setSerializerFeatures(features);
	}

	@Deprecated
	public SerializeFilter[] getFilters() {
		return this.fastJsonConfig.getSerializeFilters();
	}

	@Deprecated
	public void setFilters(SerializeFilter... filters) {
		this.fastJsonConfig.setSerializeFilters(filters);
	}
	
	/**
	 * Set renderedAttributes.
	 *
	 * @param renderedAttributes renderedAttributes
	 */
	public void setRenderedAttributes(Set<String> renderedAttributes) {
		this.renderedAttributes = renderedAttributes;
	}
	
	/**
	 * Check extractValueFromSingleKeyModel.
	 *
	 * @return extractValueFromSingleKeyModel
	 */
	public boolean isExtractValueFromSingleKeyModel() {
		return extractValueFromSingleKeyModel;
	}

	/**
	 * Set extractValueFromSingleKeyModel.
	 *
	 * @param extractValueFromSingleKeyModel
	 */
	public void setExtractValueFromSingleKeyModel(
			boolean extractValueFromSingleKeyModel) {
		this.extractValueFromSingleKeyModel = extractValueFromSingleKeyModel;
	}
	
	@Override
    protected void renderMergedOutputModel(Map<String, Object> model, //
                                           HttpServletRequest request, //
                                           HttpServletResponse response) throws Exception {
	    
		Object value = filterModel(model);
		OutputStream stream = this.updateContentLength ? createTemporaryOutputStream()
            : response.getOutputStream();
        JSON.writeJSONString(value, //
                             stream, //
                             fastJsonConfig.getCharset(), //
                             fastJsonConfig.getSerializeConfig(), //
                             fastJsonConfig.getSerializeFilters(), //
                             fastJsonConfig.getDateFormat(), //
                             JSON.DEFAULT_GENERATE_FEATURE, //
                             fastJsonConfig.getSerializerFeatures());
		
		stream.flush();
		
		if (this.updateContentLength) {
			writeToResponse(response, (ByteArrayOutputStream) stream);
		}
	}

	@Override
    protected void prepareResponse(HttpServletRequest request, //
                                   HttpServletResponse response) {
	    
		setResponseContentType(request, response);
		response.setCharacterEncoding(fastJsonConfig.getCharset().name());
		if (this.disableCaching) {
			response.addHeader("Pragma", "no-cache");
			response.addHeader("Cache-Control", "no-cache, no-store, max-age=0");
			response.addDateHeader("Expires", 1L);
		}
	}

	/**
	 * Disables caching of the generated JSON.
	 * <p>
	 * Default is {@code true}, which will prevent the client from caching the
	 * generated JSON.
	 */
	public void setDisableCaching(boolean disableCaching) {
		this.disableCaching = disableCaching;
	}

	/**
	 * Whether to update the 'Content-Length' header of the response. When set
	 * to {@code true}, the response is buffered in order to determine the
	 * content length and set the 'Content-Length' header of the response.
	 * <p>
	 * The default setting is {@code false}.
	 */
	public void setUpdateContentLength(boolean updateContentLength) {
		this.updateContentLength = updateContentLength;
	}

	/**
	 * Filters out undesired attributes from the given model. The return value
	 * can be either another {@link Map}, or a single value object.
	 * <p>
	 * Default implementation removes {@link BindingResult} instances and
	 * entries not included in the {@link #setRenderedAttributes(Set)
	 * renderedAttributes} property.
	 *
	 * @param model
	 *            the model, as passed on to {@link #renderMergedOutputModel}
	 * @return the object to be rendered
	 */
	protected Object filterModel(Map<String, Object> model) {
		Map<String, Object> result = new HashMap<String, Object>(model.size());
        Set<String> renderedAttributes = !CollectionUtils.isEmpty(this.renderedAttributes) ? //
            this.renderedAttributes //
            : model.keySet();
        
		for (Map.Entry<String, Object> entry : model.entrySet()) {
			if (!(entry.getValue() instanceof BindingResult)
					&& renderedAttributes.contains(entry.getKey())) {
				result.put(entry.getKey(), entry.getValue());
			}
		}
		if (extractValueFromSingleKeyModel) {
			if (result.size() == 1) {
				for (Map.Entry<String, Object> entry : result.entrySet()) {
					return entry.getValue();
				}
			}
		}
		return result;
	}

}