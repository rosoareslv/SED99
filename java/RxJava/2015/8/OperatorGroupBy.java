/**
 * Copyright 2015 Netflix, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See
 * the License for the specific language governing permissions and limitations under the License.
 */

package io.reactivex.internal.operators;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.*;
import java.util.function.Function;

import org.reactivestreams.*;

import io.reactivex.Observable.Operator;
import io.reactivex.internal.queue.SpscLinkedArrayQueue;
import io.reactivex.internal.subscriptions.EmptySubscription;
import io.reactivex.internal.util.BackpressureHelper;
import io.reactivex.observables.GroupedObservable;
import io.reactivex.plugins.RxJavaPlugins;

public final class OperatorGroupBy<T, K, V> implements Operator<GroupedObservable<V, K>, T>{
    final Function<? super T, ? extends K> keySelector;
    final Function<? super T, ? extends V> valueSelector;
    final int bufferSize;
    final boolean delayError;
    
    public OperatorGroupBy(Function<? super T, ? extends K> keySelector, Function<? super T, ? extends V> valueSelector, int bufferSize, boolean delayError) {
        this.keySelector = keySelector;
        this.valueSelector = valueSelector;
        this.bufferSize = bufferSize;
        this.delayError = delayError;
    }
    
    @Override
    public Subscriber<? super T> apply(Subscriber<? super GroupedObservable<V, K>> t) {
        return new GroupBySubscriber<>(t, keySelector, valueSelector, bufferSize, delayError);
    }
    
    public static final class GroupBySubscriber<T, K, V> extends AtomicInteger implements Subscriber<T>, Subscription {
        /** */
        private static final long serialVersionUID = -3688291656102519502L;
        
        final Subscriber<? super GroupedObservable<V, K>> actual;
        final Function<? super T, ? extends K> keySelector;
        final Function<? super T, ? extends V> valueSelector;
        final int bufferSize;
        final boolean delayError;
        final Map<K, GroupedUnicast<V, K>> groups;
        
        Subscription s;
        
        volatile int cancelled;
        @SuppressWarnings("rawtypes")
        static final AtomicIntegerFieldUpdater<GroupBySubscriber> CANCELLED =
                AtomicIntegerFieldUpdater.newUpdater(GroupBySubscriber.class, "cancelled");

        public GroupBySubscriber(Subscriber<? super GroupedObservable<V, K>> actual, Function<? super T, ? extends K> keySelector, Function<? super T, ? extends V> valueSelector, int bufferSize, boolean delayError) {
            this.actual = actual;
            this.keySelector = keySelector;
            this.valueSelector = valueSelector;
            this.bufferSize = bufferSize;
            this.delayError = delayError;
            this.groups = new ConcurrentHashMap<>();
        }
        
        @Override
        public void onSubscribe(Subscription s) {
            if (this.s != null) {
                s.cancel();
                RxJavaPlugins.onError(new IllegalStateException("Subscription already set!"));
                return;
            }
            
            this.s = s;
            actual.onSubscribe(this);
        }
        
        @Override
        public void onNext(T t) {
            K key;
            try {
                key = keySelector.apply(t);
            } catch (Throwable e) {
                s.cancel();
                onError(e);
                return;
            }
            
            boolean notNew = true;
            GroupedUnicast<V, K> group = groups.get(key);
            if (group == null) {
                // if the main has been cancelled, stop creating groups
                // and skip this value
                if (cancelled != 0) {
                    s.request(1);
                    return;
                }
                notNew = true;
                
                group = GroupedUnicast.createWith(key, bufferSize, this, delayError);
                groups.put(key, group);
                
                getAndIncrement();
                
                actual.onNext(group);
            }
            
            V v;
            try {
                v = valueSelector.apply(t);
            } catch (Throwable e) {
                s.cancel();
                onError(e);
                return;
            }

            group.onNext(v);
            
            if (notNew) {
                s.request(1); // we spent this t on an existing group, request one more
            }
        }
        
        @Override
        public void onError(Throwable t) {
            List<GroupedUnicast<V, K>> list = new ArrayList<>(groups.values());
            groups.clear();
            
            list.forEach(g -> g.onError(t));
            
            actual.onError(t);
        }
        
        @Override
        public void onComplete() {
            List<GroupedUnicast<V, K>> list = new ArrayList<>(groups.values());
            groups.clear();
            
            list.forEach(GroupedUnicast::onComplete);
            
            actual.onComplete();
        }

        @Override
        public void request(long n) {
            s.request(n);
        }
        
        @Override
        public void cancel() {
            // cancelling the main source means we don't want any more groups
            // but running groups still require new values
            if (CANCELLED.compareAndSet(this, 0, 1)) {
                if (decrementAndGet() == 0) {
                    s.cancel();
                }
            }
        }
        
        public void cancel(K key) {
            groups.remove(key);
            if (decrementAndGet() == 0) {
                s.cancel();
            }
        }
    }
    
    static final class GroupedUnicast<T, K> extends GroupedObservable<T, K> {
        
        public static <T, K> GroupedUnicast<T, K> createWith(K key, int bufferSize, GroupBySubscriber<?, K, T> parent, boolean delayError) {
            State<T, K> state = new State<>(bufferSize, parent, key, delayError);
            return new GroupedUnicast<>(key, state);
        }
        
        final State<T, K> state;
        
        protected GroupedUnicast(K key, State<T, K> state) {
            super(state, key);
            this.state = state;
        }
        
        public void onNext(T t) {
            state.onNext(t);
        }
        
        public void onError(Throwable e) {
            state.onError(e);
        }
        
        public void onComplete() {
            state.onComplete();
        }
    }
    
    static final class State<T, K> extends AtomicInteger implements Subscription, Publisher<T> {
        /** */
        private static final long serialVersionUID = -3852313036005250360L;

        final K key;
        final Queue<T> queue;
        final GroupBySubscriber<?, K, T> parent;
        final boolean delayError;
        
        volatile long requested;
        @SuppressWarnings("rawtypes")
        static final AtomicLongFieldUpdater<State> REQUESTED =
                AtomicLongFieldUpdater.newUpdater(State.class, "requested");
        
        volatile boolean done;
        Throwable error;
        
        volatile int cancelled;
        @SuppressWarnings("rawtypes")
        static final AtomicIntegerFieldUpdater<State> CANCELLED =
                AtomicIntegerFieldUpdater.newUpdater(State.class, "cancelled");
        
        volatile Subscriber<? super T> actual;
        @SuppressWarnings("rawtypes")
        static final AtomicReferenceFieldUpdater<State, Subscriber> ACTUAL =
                AtomicReferenceFieldUpdater.newUpdater(State.class, Subscriber.class, "actual");
        
        public State(int bufferSize, GroupBySubscriber<?, K, T> parent, K key, boolean delayError) {
            this.queue = new SpscLinkedArrayQueue<>(bufferSize);
            this.parent = parent;
            this.key = key;
            this.delayError = delayError;
        }
        
        @Override
        public void request(long n) {
            if (n <= 0) {
                RxJavaPlugins.onError(new IllegalArgumentException("n > required but it was " + n));
                return;
            }
            BackpressureHelper.add(REQUESTED, this, n);
            drain();
        }
        
        @Override
        public void cancel() {
            if (CANCELLED.compareAndSet(this, 0, 1)) {
                if (getAndIncrement() == 0) {
                    parent.cancel(key);
                }
            }
        }
        
        @Override
        public void subscribe(Subscriber<? super T> s) {
            if (ACTUAL.compareAndSet(this, null, s)) {
                s.onSubscribe(this);
            } else {
                EmptySubscription.error(new IllegalStateException("Only one Subscriber allowed!"), s);
            }
        }

        public void onNext(T t) {
            if (t == null) {
                parent.cancel(key);
                error = new NullPointerException();
                done = true;
            } else {
                queue.offer(t);
            }
            drain();
        }
        
        public void onError(Throwable e) {
            error = e;
            done = true;
            drain();
        }
        
        public void onComplete() {
            done = true;
            drain();
        }

        void drain() {
            if (getAndIncrement() != 0) {
                return;
            }
            int missed = 1;
            
            final Queue<T> q = queue;
            final boolean delayError = this.delayError;
            Subscriber<? super T> a = actual;
            for (;;) {
                if (a != null) {
                    if (checkTerminated(done, q.isEmpty(), a, delayError)) {
                        return;
                    }
                    
                    long r = requested;
                    boolean unbounded = r == Long.MAX_VALUE;
                    long e = 0;
                    
                    while (r != 0L) {
                        boolean d = done;
                        T v = q.poll();
                        boolean empty = v == null;
                        
                        if (checkTerminated(d, empty, a, delayError)) {
                            return;
                        }
                        
                        if (empty) {
                            break;
                        }
                        
                        r--;
                        e--;
                    }
                    
                    if (e != 0L) {
                        if (!unbounded) {
                            REQUESTED.addAndGet(this, e);
                        }
                    }
                }
                
                missed = addAndGet(-missed);
                if (missed == 0) {
                    break;
                }
                if (a == null) {
                    a = actual;
                }
            }
        }
        
        boolean checkTerminated(boolean d, boolean empty, Subscriber<? super T> a, boolean delayError) {
            if (cancelled != 0) {
                parent.cancel(key);
                return true;
            }
            
            if (d) {
                if (delayError) {
                    if (empty) {
                        Throwable e = error;
                        if (e != null) {
                            a.onError(e);
                        } else {
                            a.onComplete();
                        }
                        return true;
                    }
                } else {
                    Throwable e = error;
                    if (e != null) {
                        queue.clear();
                        a.onError(e);
                        return true;
                    } else
                    if (empty) {
                        a.onComplete();
                        return true;
                    }
                }
            }
            
            return false;
        }
    }
}
