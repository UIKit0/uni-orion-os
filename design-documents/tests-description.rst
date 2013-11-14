alarm_priority
--------------
Source: tests/threads/alarm-priority.c
Purpose: Checks that when the alarm clock wakes up threads, the higher-priority threads run first.
Description: The test starts 10 threads with varying priorities. Each thread is put to sleep and scheduled to wake up at the same time. The scheduler must wake the threads up in order of their priority.
Note: timer_ticks () + 5 * TIMER_FREQ looks like it's only 5 ticks away from the current time. Check if it's time enough for each thread to sleep.

alarm-negative
--------------
Source: tests/threads/alarm-negative.c
Purpose: Checks that the OS doesn't crash when a thread is put to sleep for a negative ammount of ticks.
Description: The current thread is put to sleep for -100 ticks.

alarm-zero
----------
Source: tests/threads/alarm-zero.c
Purpose: Checks that the OS doesn't put the thread to sleep at all if timer_sleep is called with a value of 0 ticks.
Description: The current thread is put to sleep for 0 ticks. No checks are made.

alarm-simultaneous
------------------
Source: tests/threads/alarm-simultaneous.c
Purpose: Creates N threads, each of which sleeps a different, fixed duration, M times. Records the wake-up order and verifies that it is valid.
Description: Each thread is put to sleep 5 times and scheduled to wake up at the same time as all the other threads. The test records the wake times for each iteration relative to the first thread. The first thread must wake up 10 ticks later (relative to it's own previous wakeup time). The rest of the treads must wake up during the same tick as the first (0 ticks relative to the first).

alarm-multiple
--------------
Source: tests/threads/alarm-wait.c
Purpose: Creates N threads, each of which sleeps a different, fixed duration, M times. Records the wake-up order and verifies that it is valid.
Description: Starts 5 threads, each sleeping for thread_id * 10 ticks each iteration for 7 iterations. The wakeup time for each thread is given by sleep_duration * iteration for each iteration. The test checks that the wake up times are in ascending order (the threads did indeed wake up at the right times).

alarm-single
------------
Source: tests/threads/alarm-wait.c
Purpose: Creates N threads, each of which sleeps a different, fixed duration, M times. Records the wake-up order and verifies that it is valid.
Description: Uses the same test as alarm-multiple, but with only one iteration per thread.

alarm-priority
--------------