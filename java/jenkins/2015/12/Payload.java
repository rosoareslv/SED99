/*
 * The MIT License
 *
 * Copyright (c) 2015 Oleg Nenashev.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
package jenkins.security.security218;

import jenkins.security.security218.ysoserial.payloads.CommonsCollections1;
import jenkins.security.security218.ysoserial.payloads.CommonsCollections2;
import jenkins.security.security218.ysoserial.payloads.Groovy1;
import jenkins.security.security218.ysoserial.payloads.ObjectPayload;
import jenkins.security.security218.ysoserial.payloads.Spring1;


/**
 * Allows to select {@link ObjectPayload}s.
 * @author Oleg Nenashev
 */
public enum Payload {
    CommonsCollections1(CommonsCollections1.class),
    CommonsCollections2(CommonsCollections2.class),
    Groovy1(Groovy1.class),
    Spring1(Spring1.class);
    
    private final Class<? extends ObjectPayload> payloadClass;
    
    private Payload(Class<? extends ObjectPayload> payloadClass) {
        this.payloadClass = payloadClass;
    }

    public Class<? extends ObjectPayload> getPayloadClass() {
        return payloadClass;
    }
}
