/*
 * Copyright (c) 2007 Mockito contributors
 * This program is made available under the terms of the MIT License.
 */
package org.mockito.internal.util;

import org.mockito.MockingDetails;
import org.mockito.exceptions.misusing.NotAMockException;
import org.mockito.internal.InternalMockHandler;
import org.mockito.invocation.Invocation;
import org.mockito.mock.MockCreationSettings;

import java.util.Collection;

import static org.mockito.internal.util.MockUtil.getMockHandler;

/**
 * Class to inspect any object, and identify whether a particular object is either a mock or a spy.  This is
 * a wrapper for {@link org.mockito.internal.util.MockUtil}.
 */
public class DefaultMockingDetails implements MockingDetails {

    private final Object toInspect;

    public DefaultMockingDetails(Object toInspect){
        this.toInspect = toInspect;
    }

    @Override
    public boolean isMock(){
        return MockUtil.isMock(toInspect);
    }

    @Override
    public boolean isSpy(){
        return MockUtil.isSpy(toInspect);
    }

    @Override
    public Collection<Invocation> getInvocations() {
        return mockHandler().getInvocationContainer().getInvocations();
    }

    @Override
    public MockCreationSettings<?> getMockCreationSettings() {
        return mockHandler().getMockSettings();
    }

    private InternalMockHandler<Object> mockHandler() {
        assertGoodMock();
        return getMockHandler(toInspect);
    }

    private void assertGoodMock() {
        if (toInspect == null) {
            throw new NotAMockException("Argument passed to Mockito.mockingDetails() should be a mock, but is null!");
        } else if (!isMock()) {
            throw new NotAMockException("Argument passed to Mockito.mockingDetails() should be a mock, but is an instance of " + toInspect.getClass() + "!");
        }
    }
}

