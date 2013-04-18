//
//  run_loop.cpp
//  ePub3
//
//  Created by Jim Dovey on 2013-04-08.
//  Copyright (c) 2013 The Readium Foundation and contributors. All rights reserved.
//

#include "run_loop.h"

#if EPUB_OS(ANDROID)
# include <semaphore.h>
#endif

EPUB3_BEGIN_NAMESPACE

static pthread_key_t RunLoopTLSKey;

static void _DestroyTLSRunLoop(void* data)
{
    RunLoop* rl = reinterpret_cast<RunLoop*>(data);
    delete rl;
}
/// FIXME: alternatives to __attribute__((constructor)) etc.?
__attribute__((constructor))
static void InitRunLoopTLSKey()
{
    pthread_key_create(&RunLoopTLSKey, _DestroyTLSRunLoop);
}
__attribute__((destructor))
static void KillRunLoopTLSKey()
{
    pthread_key_delete(RunLoopTLSKey);
}

RunLoop* RunLoop::CurrentRunLoop()
{
    RunLoop* p = reinterpret_cast<RunLoop*>(pthread_getspecific(RunLoopTLSKey));
    if ( p == nullptr )
    {
        p = new RunLoop();
        pthread_setspecific(RunLoopTLSKey, reinterpret_cast<void*>(p));
    }
    return p;
}

#if EPUB_USE(CF)

#define ADD_MODE_ARG kCFRunLoopCommonModes
#define RUN_MODE_ARG kCFRunLoopDefaultMode

typedef std::chrono::duration<CFTimeInterval>   CFInterval;
typedef std::chrono::duration<CFAbsoluteTime>   CFAbsolute;

const cf_adopt_ref_t cf_adopt_ref = cf_adopt_ref_t();

cf_clock::time_point cf_clock::now() noexcept
{
    return time_point(duration(CFAbsoluteTimeGetCurrent()));
}
time_t cf_clock::to_time_t(const time_point &__t) noexcept
{
    return time_t(__t.time_since_epoch().count() + kCFAbsoluteTimeIntervalSince1970);
}
cf_clock::time_point cf_clock::from_time_t(const time_t &__t) noexcept
{
    return time_point(duration(CFAbsoluteTime(__t) - kCFAbsoluteTimeIntervalSince1970));
}

RunLoop::RunLoop() : _cf(CFRunLoopGetCurrent())
{
}
RunLoop::~RunLoop()
{
}
void RunLoop::PerformFunction(std::function<void ()> fn)
{
    CFRunLoopPerformBlock(_cf, ADD_MODE_ARG, ^{fn();});
}
void RunLoop::AddTimer(Timer* timer)
{
    CFRunLoopAddTimer(_cf, timer->_cf, ADD_MODE_ARG);
}
bool RunLoop::ContainsTimer(Timer* timer) const
{
    return (CFRunLoopContainsTimer(_cf, timer->_cf, ADD_MODE_ARG) == TRUE);
}
void RunLoop::RemoveTimer(Timer* timer)
{
    CFRunLoopRemoveTimer(_cf, timer->_cf, ADD_MODE_ARG);
}
void RunLoop::AddEventSource(EventSource* source)
{
    CFRunLoopAddSource(_cf, source->_cf, ADD_MODE_ARG);
}
bool RunLoop::ContainsEventSource(EventSource* source) const
{
    return (CFRunLoopContainsSource(_cf, source->_cf, ADD_MODE_ARG) == TRUE);
}
void RunLoop::RemoveEventSource(EventSource* source)
{
    CFRunLoopRemoveSource(_cf, source->_cf, ADD_MODE_ARG);
}
void RunLoop::AddObserver(Observer* observer)
{
    CFRunLoopAddObserver(_cf, observer->_cf, ADD_MODE_ARG);
}
bool RunLoop::ContainsObserver(Observer* observer) const
{
    return (CFRunLoopContainsObserver(_cf, observer->_cf, ADD_MODE_ARG) == TRUE);
}
void RunLoop::RemoveObserver(Observer* observer)
{
    CFRunLoopRemoveObserver(_cf, observer->_cf, ADD_MODE_ARG);
}
void RunLoop::Run()
{
    if (CFRunLoopGetCurrent() != _cf)
        return;     // Q: Should I throw here?
    CFRunLoopRun();
}
void RunLoop::Stop()
{
    CFRunLoopStop(_cf);
}
bool RunLoop::IsWaiting() const
{
    return CFRunLoopIsWaiting(_cf);
}
void RunLoop::WakeUp()
{
    CFRunLoopWakeUp(_cf);
}
RunLoop::ExitReason RunLoop::RunInternal(bool returnAfterSourceHandled, std::chrono::nanoseconds& timeout)
{
    using namespace std::chrono;
    return ExitReason(CFRunLoopRunInMode(RUN_MODE_ARG, duration_cast<cf_clock::duration>(timeout).count(), returnAfterSourceHandled));
}

RunLoop::Timer::Timer(Clock::time_point& fireDate, Clock::duration& interval, TimerFn fn)
{
    using namespace std::chrono;
    _cf = CFRunLoopTimerCreateWithHandler(kCFAllocatorDefault, fireDate.time_since_epoch().count(), interval.count(), 0, 0, ^(CFRunLoopTimerRef timer) {
        fn(*this);
    });
}
RunLoop::Timer::Timer(Clock::duration& interval, bool repeat, TimerFn fn)
{
    using namespace std::chrono;
    CFAbsoluteTime fireDate = CFAbsoluteTimeGetCurrent() + interval.count();
    _cf = CFRunLoopTimerCreateWithHandler(kCFAllocatorDefault, fireDate, interval.count(), 0, 0, ^(CFRunLoopTimerRef timer) {
        fn(*this);
    });
}
RunLoop::Timer::Timer(const Timer& o) : _cf(o._cf)
{
}
RunLoop::Timer::Timer(Timer&& o) : _cf(std::move(o._cf))
{
}
RunLoop::Timer::~Timer()
{
}
RunLoop::Timer& RunLoop::Timer::operator=(const Timer & o)
{
    _cf = o._cf;
    CFRetain(_cf);
    return *this;
}
RunLoop::Timer& RunLoop::Timer::operator=(Timer&& o)
{
    _cf.swap(std::move(o._cf));
    return *this;
}
bool RunLoop::Timer::operator==(const Timer& o) const
{
    return _cf == o._cf;
}
void RunLoop::Timer::Cancel()
{
    CFRunLoopTimerInvalidate(_cf);
}
bool RunLoop::Timer::IsCancelled() const
{
    return (CFRunLoopTimerIsValid(_cf) == FALSE);
}
bool RunLoop::Timer::Repeats() const
{
    return CFRunLoopTimerDoesRepeat(_cf);
}
RunLoop::Timer::Clock::duration RunLoop::Timer::RepeatIntervalInternal() const
{
    return Clock::duration(CFRunLoopTimerGetInterval(_cf));
}
RunLoop::Timer::Clock::time_point RunLoop::Timer::GetNextFireDateTime() const
{
    return Clock::time_point(Clock::duration(CFRunLoopTimerGetNextFireDate(_cf)));
}
void RunLoop::Timer::SetNextFireDateTime(Clock::time_point& when)
{
    CFRunLoopTimerSetNextFireDate(_cf, when.time_since_epoch().count());
}
RunLoop::Timer::Clock::duration RunLoop::Timer::GetNextFireDateDuration() const
{
    return Clock::duration(CFRunLoopTimerGetNextFireDate(_cf)) - Clock::now().time_since_epoch();
}
void RunLoop::Timer::SetNextFireDateDuration(Clock::duration& when)
{
    CFRunLoopTimerSetNextFireDate(_cf, (Clock::now()+when).time_since_epoch().count());
}

RunLoop::Observer::Observer(Activity activities, bool repeats, ObserverFn fn)
{
    _cf = CFRunLoopObserverCreateWithHandler(kCFAllocatorDefault, CFOptionFlags(activities), Boolean(repeats), 0, ^(CFRunLoopObserverRef observer, CFRunLoopActivity activity) {
        fn(*this, activities);
    });
}
RunLoop::Observer::Observer(const Observer& o) : _cf(o._cf)
{
}
RunLoop::Observer::Observer(Observer&& o) : _cf(std::move(o._cf))
{
}
RunLoop::Observer::~Observer()
{
}
RunLoop::Observer& RunLoop::Observer::operator=(const Observer & o)
{
    _cf = o._cf;
    return *this;
}
RunLoop::Observer& RunLoop::Observer::operator=(Observer &&o)
{
    _cf.swap(std::move(o._cf));
    return *this;
}
bool RunLoop::Observer::operator==(const Observer &o) const
{
    return _cf == o._cf;
}
RunLoop::Observer::Activity RunLoop::Observer::GetActivities() const
{
    return Activity(CFRunLoopObserverGetActivities(_cf));
}
bool RunLoop::Observer::Repeats() const
{
    return (CFRunLoopObserverDoesRepeat(_cf) == TRUE);
}
bool RunLoop::Observer::IsCancelled() const
{
    return (CFRunLoopObserverIsValid(_cf) == FALSE);
}
void RunLoop::Observer::Cancel()
{
    CFRunLoopObserverInvalidate(_cf);
}

RunLoop::EventSource::EventSource(EventHandlerFn fn) : _cf(nullptr), _rl(), _fn()
{
    CFRunLoopSourceContext ctx = {
        .version            = 0,
        .info               = reinterpret_cast<void*>(this),
        .retain             = nullptr,
        .release            = nullptr,
        .copyDescription    = nullptr,
        .equal              = nullptr,
        .hash               = nullptr,
        .schedule           = &_ScheduleCF,
        .cancel             = &_CancelCF,
        .perform            = &_FireCFSourceEvent,
    };
    _cf = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
}
RunLoop::EventSource::EventSource(const EventSource& o) : _cf(o._cf)
{
}
RunLoop::EventSource::EventSource(EventSource&& o) : _cf(std::move(o._cf))
{
}
RunLoop::EventSource::~EventSource()
{
}
RunLoop::EventSource& RunLoop::EventSource::operator=(EventSource && o)
{
    _cf.swap(std::move(o._cf));
    return *this;
}
bool RunLoop::EventSource::operator==(const EventSource & o) const
{
    return _cf == o._cf;
}
bool RunLoop::EventSource::IsCancelled() const
{
    return (CFRunLoopSourceIsValid(_cf) == FALSE);
}
void RunLoop::EventSource::Cancel()
{
    CFRunLoopSourceInvalidate(_cf);
}
void RunLoop::EventSource::Signal()
{
    CFRunLoopSourceSignal(_cf);
    for ( auto& item : _rl )
    {
        if ( item.second > 0 )
            CFRunLoopWakeUp(item.first);
    }
}
void RunLoop::EventSource::_FireCFSourceEvent(void *info)
{
    EventSource* p = reinterpret_cast<EventSource*>(info);
    p->_fn(*p);
}
void RunLoop::EventSource::_ScheduleCF(void* info, CFRunLoopRef rl, CFStringRef mode)
{
    EventSource* p = reinterpret_cast<EventSource*>(info);
    p->_rl[rl]++;
}
void RunLoop::EventSource::_CancelCF(void* info, CFRunLoopRef rl, CFStringRef mode)
{
    EventSource* p = reinterpret_cast<EventSource*>(info);
    if ( --(p->_rl[rl]) == 0 )
    {
        p->_rl.erase(rl);
    }
}

#elif EPUB_OS(ANDROID)

using StackLock = std::lock_guard<std::recursive_mutex>;

RunLoop::RunLoop() : _timers(), _observers(), _sources(), _listLock(), _conditionLock(), _wakeUp(), _waiting(false), _stop(false), _observerMask(0), _waitingUntilTimer(nullptr)
{
}
RunLoop::~RunLoop()
{
    if ( _waiting )
        Stop();
    
    for ( auto p : _timers )
    {
        if ( p != nullptr )
            delete p;
    }
    for ( auto p : _observers )
    {
        if ( p != nullptr )
            delete p;
    }
    for ( auto p : _sources )
    {
        if ( p != nullptr )
            delete p;
    }
}
void RunLoop::PerformFunction(std::function<void ()> fn)
{
    EventSource* ev = new EventSource([fn](EventSource& __e) {
        fn();
    });
    AddEventSource(ev);
    ev->Signal();
}
void RunLoop::AddTimer(Timer* timer)
{
    StackLock lock(_listLock);
    if ( ContainsTimer(timer) )
        return;
    
    _timers.push_back(timer);
    _timers.sort();
    
    if ( _waiting && _waitingUntilTimer != nullptr && _waitingUntilTimer->GetNextFireDate() < timer->GetNextFireDate() )
    {
        // signal a Run() invocation that it needs to adjust its timeout to the fire
        // date of this new timer
        WakeUp();
    }
}
bool RunLoop::ContainsTimer(Timer* timer) const
{
    StackLock lock(const_cast<RunLoop*>(this)->_listLock);
    for ( const Timer* t : _timers )
    {
        if ( timer == t )
            return true;
    }
    return false;
}
void RunLoop::RemoveTimer(Timer* timer)
{
    StackLock lock(_listLock);
    for ( auto iter = _timers.begin(), end = _timers.end(); iter != end; ++iter )
    {
        if ( *iter == timer )
        {
            _timers.erase(iter);
            break;
        }
    }
    
    if ( _waiting )
    {
        if ( _waitingUntilTimer == timer )
        {
            // a Run() invocation is waiting until the removed timer's fire date
            // wake up the runloop so it can adjust its timeout accordingly
            _waitingUntilTimer = nullptr;
            WakeUp();
        }
        else if ( _timers.empty() && _sources.empty() )
        {
            // run out of useful things to wait upon
            WakeUp();
        }
    }
}
void RunLoop::AddObserver(Observer* observer)
{
    StackLock lock(_listLock);
    if ( ContainsObserver(observer) )
        return;
    
    _observers.push_back(observer);
    _observerMask |= observer->_acts;
}
bool RunLoop::ContainsObserver(Observer* obs) const
{
    StackLock lock(const_cast<RunLoop*>(this)->_listLock);
    for ( const Observer* o : _observers )
    {
        if ( obs == o )
            return true;
    }
    return false;
}
void RunLoop::RemoveObserver(Observer* obs)
{
    StackLock lock(_listLock);
    for ( auto iter = _observers.begin(), end = _observers.end(); iter != end; ++iter )
    {
        if ( *iter == obs )
        {
            _observers.erase(iter);
            break;
        }
    }
}
void RunLoop::AddEventSource(EventSource* ev)
{
    StackLock lock(_listLock);
    if ( ContainsEventSource(ev) )
        return;
    
    _sources.push_back(ev);
}
bool RunLoop::ContainsEventSource(EventSource* ev) const
{
    StackLock lock(const_cast<RunLoop*>(this)->_listLock);
    for ( const EventSource* e : _sources )
    {
        if ( ev == e )
            return true;
    }
    return false;
}
void RunLoop::RemoveEventSource(EventSource* ev)
{
    StackLock lock(_listLock);
    for ( auto iter = _sources.begin(), end = _sources.end(); iter != end; ++iter )
    {
        if ( *iter == ev )
        {
            _sources.erase(iter);
            break;
        }
    }
    
    if ( _waiting && _timers.empty() && _sources.empty() )
    {
        // run out of useful things to wait upon
        WakeUp();
    }
}
void RunLoop::Run()
{
    ExitReason reason;
    do
    {
        std::chrono::nanoseconds timeout(std::chrono::duration_values<std::chrono::nanoseconds::rep>::max());
        reason = RunInternal(false, timeout);
        
    } while (reason != ExitReason::RunStopped && reason != ExitReason::RunFinished);
}
void RunLoop::Stop()
{
    _stop = true;
    if ( _waiting )
        WakeUp();
}
bool RunLoop::IsWaiting() const
{
    return _waiting;
}
void RunLoop::WakeUp()
{
    if ( _conditionLock.try_lock() )
    {
        _wakeUp.notify_all();
        _conditionLock.unlock();
    }
}
RunLoop::ExitReason RunLoop::RunInternal(bool returnAfterSourceHandled, std::chrono::nanoseconds &timeout)
{
    using namespace std::chrono;
    system_clock::time_point timeoutTime = system_clock::now() + duration_cast<system_clock::duration>(timeout);
    ExitReason reason(ExitReason::RunTimedOut);
    
    // catch a pending stop
    if ( _stop.exchange(false) )
        return ExitReason::RunStopped;
    
    _listLock.lock();
    
    do
    {
        RunObservers(Observer::ActivityFlags::RunLoopEntry);
        
        std::vector<Timer*> timersToFire = CollectFiringTimers();
        if ( !timersToFire.empty() )
        {
            RunObservers(Observer::ActivityFlags::RunLoopBeforeTimers);
            for ( auto timer : timersToFire )
            {
                // we'll reset repeating timers after the callback returns, so it doesn't
                // arm again while we're 
                Timer::Clock::time_point date = timer->GetNextFireDate();
                
                // fire the callback now
                timer->_fn(*timer);
                
                // only reset a repeating timer if the fire date hasn't been changed
                //  by the callback
                if ( timer->Repeats() && timer->GetNextFireDate() == date )
                {
                    timer->SetNextFireDate(timer->_interval);
                }
            }
        }
        
        std::vector<EventSource*> sourcesToFire = CollectFiringSources(returnAfterSourceHandled);
        if ( !sourcesToFire.empty() )
        {
            RunObservers(Observer::ActivityFlags::RunLoopBeforeSources);
            for ( auto source : sourcesToFire )
            {
                source->_fn(*source);
            }
            
            if ( returnAfterSourceHandled )
            {
                reason = ExitReason::RunHandledSource;
                break;
            }
        }
        
        if ( timeout <= nanoseconds(0) )
        {
            reason = ExitReason::RunTimedOut;
            break;
        }
        
        RunObservers(Observer::ActivityFlags::RunLoopBeforeWaiting);
        _listLock.unlock();
        _waiting = true;
        
        system_clock::time_point waitUntil = TimeoutOrTimer(timeoutTime);
        
        std::unique_lock<std::mutex> _condLock(_conditionLock);
        std::cv_status waitStatus = _wakeUp.wait_until(_condLock, waitUntil);
        _condLock.unlock();
        
        _waiting = false;
        _listLock.lock();
        
        RunObservers(Observer::ActivityFlags::RunLoopAfterWaiting);
        
        // why did we wake up?
        if ( waitStatus == std::cv_status::timeout )
        {
            reason = ExitReason::RunTimedOut;
            break;
        }
        
        if ( _stop )
        {
            reason = ExitReason::RunStopped;
            break;
        }
    
    } while (timeoutTime > system_clock::now());
    
    _listLock.unlock();
    return reason;
}
void RunLoop::RunObservers(Observer::Activity activity)
{
    // _listLock MUST ALREADY BE HELD
    if ( (_observerMask & activity) == 0 )
        return;
    
    std::vector<Observer*> observersToRemove;
    for ( auto observer : _observers )
    {
        if ( observer->IsCancelled() )
        {
            observersToRemove.push_back(observer);
            continue;
        }
        
        if ( (observer->_acts & activity) == 0 )
            continue;
        observer->_fn(*observer, activity);
        if ( !observer->Repeats() )
            observersToRemove.push_back(observer);
    }
    
    for ( auto observer : observersToRemove )
    {
        RemoveObserver(observer);
    }
}
std::vector<RunLoop::Timer*> RunLoop::CollectFiringTimers()
{
    // _listLock MUST ALREADY BE HELD
    auto currentTime = std::chrono::system_clock::now();
    std::vector<Timer*> result;
    
    std::vector<Timer*> timersToRemove;
    for ( Timer* timer : _timers )
    {
        if ( timer->IsCancelled() )
        {
            timersToRemove.push_back(timer);
            continue;
        }
        
        // Timers are sorted in ascending order, so as soon as we've encountered one
        // which hasn't yet fired, we can stop searching.
        if ( timer->GetNextFireDate() > currentTime )
            break;
        
        result.push_back(timer);
    }
    
    for ( auto timer : timersToRemove )
    {
        RemoveTimer(timer);
    }
    
    return result;
}
std::vector<RunLoop::EventSource*> RunLoop::CollectFiringSources(bool onlyOne)
{
    // _listLock MUST ALREADY BE HELD
    std::vector<EventSource*> result;
    
    std::vector<EventSource*> cancelledSources;
    for ( EventSource* source : _sources )
    {
        if ( source->IsCancelled() )
        {
            cancelledSources.push_back(source);
            continue;
        }
        
        // we atomically set it to false while reading to ensure only one RunLoop
        // picks up the source
        if ( source->_signalled.exchange(false) )
            result.push_back(source);
        
        if ( onlyOne )
            break;      // don't unset the signal on any other sources
    }
    
    for ( auto source : cancelledSources )
    {
        RemoveEventSource(source);
    }
    
    return result;
}
std::chrono::system_clock::time_point RunLoop::TimeoutOrTimer(std::chrono::system_clock::time_point& timeout)
{
    // _listLock MUST ALREADY BE HELD
    if ( _timers.empty() )
        return timeout;
    
    std::chrono::system_clock::time_point fireDate = _timers.front()->GetNextFireDate();
    if ( fireDate < timeout )
    {
        _waitingUntilTimer = _timers.front();
        return fireDate;
    }
    
    return timeout;
}

RunLoop::Observer::Observer(Activity activities, bool repeats, ObserverFn fn) : _fn(fn), _acts(activities), _repeats(repeats), _cancelled(false)
{
}
RunLoop::Observer::Observer(const Observer& o) : _fn(o._fn), _acts(o._acts), _repeats(o._repeats)
{
}
RunLoop::Observer::Observer(Observer&& o) : _fn(std::move(o._fn)), _acts(o._acts), _repeats(o._repeats)
{
    o._acts = 0;
    o._repeats = false;
}
RunLoop::Observer::~Observer()
{
}
RunLoop::Observer& RunLoop::Observer::operator=(const Observer& o)
{
    _fn = o._fn;
    _acts = o._acts;
    _repeats = o._repeats;
    return *this;
}
RunLoop::Observer& RunLoop::Observer::operator=(Observer&& o)
{
    _fn = std::move(o._fn);
    _acts = o._acts; o._acts = 0;
    _repeats = o._repeats; o._repeats = false;
    return *this;
}
bool RunLoop::Observer::operator==(const Observer& o) const
{
    // cast as void* to compare function addresses
    return _fn.target<void>() == o._fn.target<void>();
}
RunLoop::Observer::Activity RunLoop::Observer::GetActivities() const
{
    return _acts;
}
bool RunLoop::Observer::Repeats() const
{
    return _repeats;
}
bool RunLoop::Observer::IsCancelled() const
{
    return _cancelled;
}
void RunLoop::Observer::Cancel()
{
    _cancelled = true;
}

RunLoop::EventSource::EventSource(EventHandlerFn fn) : _signalled(false), _cancelled(false), _fn(fn)
{
}
RunLoop::EventSource::EventSource(const EventSource& o) : _signalled((bool)o._signalled), _cancelled(o._cancelled), _fn(o._fn)
{
}
RunLoop::EventSource::EventSource(EventSource&& o) : _signalled(o._signalled.exchange(false)), _cancelled(o._cancelled), _fn(std::move(o._fn))
{
    o._cancelled = false;
}
RunLoop::EventSource::~EventSource()
{
}
RunLoop::EventSource& RunLoop::EventSource::operator=(const EventSource& o)
{
    _signalled = (bool)o._signalled;
    _fn = o._fn;
    _cancelled = o._cancelled;
    return *this;
}
RunLoop::EventSource& RunLoop::EventSource::operator=(EventSource&& o)
{
    _signalled = o._signalled.exchange(false);
    _fn = std::move(o._fn);
    _cancelled = o._cancelled; o._cancelled = false;
    return *this;
}
bool RunLoop::EventSource::operator==(const EventSource& o) const
{
    return _fn.target<void>() == o._fn.target<void>();
}
bool RunLoop::EventSource::IsCancelled() const
{
    return _cancelled;
}
void RunLoop::EventSource::Cancel()
{
    _cancelled = true;
}
void RunLoop::EventSource::Signal()
{
    _signalled = true;
}

RunLoop::Timer::Timer(Clock::time_point& fireDate, Clock::duration& interval, TimerFn fn) : _fireDate(fireDate), _interval(interval), _fn(fn)
{
}
RunLoop::Timer::Timer(Clock::duration& interval, bool repeat, TimerFn fn) : _fireDate(Clock::now()+interval), _interval(interval), _fn(fn)
{
}
RunLoop::Timer::Timer(const Timer& o) : _fireDate(o._fireDate), _interval(o._interval), _fn(o._fn)
{
}
RunLoop::Timer::Timer(Timer&& o) : _fireDate(std::move(o._fireDate)), _interval(std::move(o._interval)), _fn(std::move(o._fn))
{
}
RunLoop::Timer::~Timer()
{
}
RunLoop::Timer& RunLoop::Timer::operator=(const Timer& o)
{
    _fireDate = o._fireDate;
    _interval = o._interval;
    _fn = o._fn;
    return *this;
}
RunLoop::Timer& RunLoop::Timer::operator=(Timer&& o)
{
    _fireDate = std::move(o._fireDate);
    _interval = std::move(o._interval);
    _fn = std::move(o._fn);
    return *this;
}
bool RunLoop::Timer::operator==(const Timer& o) const
{
    return (_fireDate == o._fireDate) && (_interval == o._interval) && (_fn.target<void>() == o._fn.target<void>());
}
void RunLoop::Timer::Cancel()
{
    _cancelled = true;
}
bool RunLoop::Timer::IsCancelled() const
{
    return _cancelled;
}
bool RunLoop::Timer::Repeats() const
{
    return _interval > Clock::duration(0);
}
RunLoop::Timer::Clock::duration RunLoop::Timer::RepeatIntervalInternal() const
{
    return _interval;
}
RunLoop::Timer::Clock::time_point RunLoop::Timer::GetNextFireDateTime() const
{
    return _fireDate;
}
void RunLoop::Timer::SetNextFireDateTime(Clock::time_point& when)
{
    _fireDate = when;
}
RunLoop::Timer::Clock::duration RunLoop::Timer::GetNextFireDateDuration() const
{
    return _fireDate - Clock::now();
}
void RunLoop::Timer::SetNextFireDateDuration(Clock::duration& when)
{
    _fireDate = Clock::now() + when;
}

#elif EPUB_OS(WINDOWS)
# error RunLoop on Windows missing implementation
#else
# error Don't know how to implement a RunLoop on this system
#endif

EPUB3_END_NAMESPACE
