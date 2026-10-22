# Scheduling hinted tasks

`Task::setPreferredWorker(id)` sets a worker hint. The scheduler stores a fresh `AffinityTask` record with the task, its preferred worker ID, and an atomic claim flag. It places references to that record in the preferred worker's FIFO mailbox and stealable work queue.

The task body stays in the shared record. A worker must change the claim flag from false to true before it calls the body. The winner runs the task once. The other queue reference later fails the claim and is discarded. A new record is allocated for every hinted task, so a later task cannot reuse an earlier generation's identity.

The preferred worker drains its mailbox before its regular task queue. Idle peers receive a condition-variable signal and enter Marl's bounded steal loop. Each worker's mailbox and normal queue use that worker's existing mutex. Enqueueing releases the preferred worker's mutex before it signals peers, so it does not hold multiple worker locks at once.

`SameThread` takes precedence over a worker hint and remains on the scheduling thread. Suspended fibers remain attached to their owning worker and resume there. A worker hint does not pin a task. A busy preferred worker can lose work to a peer, and a single-threaded scheduler ignores the hint.

The design follows the per-worker affinity mailbox and duplicate queue entry from Acar, Blelloch, and Blumofe's [The Data Locality of Work Stealing](https://www.cs.cmu.edu/~blelloch/papers/locality2000.pdf), section 7.1. This bounded implementation does not establish the paper's theorem or reproduce its speedup results. Queue locks remain in place. The shared record and duplicate queue nodes add allocation and reference-counting costs.
