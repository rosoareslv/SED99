package com.alibaba.json.bvt.jdk7;

import java.nio.file.Path;
import java.nio.file.Paths;

import org.junit.Assert;

import com.alibaba.fastjson.JSON;

import junit.framework.TestCase;

public class PathTest extends TestCase {
    public void test_for_path() throws Exception {
        Model model = new Model();
        model.path = Paths.get("/root/fastjson");
        
        String text = JSON.toJSONString(model);
        
        Assert.assertEquals("{\"path\":\"/root/fastjson\"}", text);
        
        Model model2 = JSON.parseObject(text, Model.class);
        Assert.assertEquals(model.path.toString(), model2.path.toString());
    }
    
    public static class Model {
        public Path path;
    }
}
