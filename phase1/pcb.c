#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];

/* Initialize the pcbFree list to contain all the elements of the
 * static array of MAXPROC PCBs. This method will be called only once during
 * data structure initialization. */
void initPcbs() {
  INIT_LIST_HEAD(&pcbFree_h);

  for (int i = 0; i < MAXPROC; i++) {
    list_add(&pcbFree_table[i].p_list, &pcbFree_h);
  }
}

/* Insert the element pointed to by p onto the pcbFree list. */
void freePcb(pcb_t *p) {
  if (p != NULL)
    list_add(&p->p_list, &pcbFree_h);
}

/* Return NULL if the pcbFree list is empty. Otherwise, remove an element from
 * the pcbFree list, provide initial values for ALL of the PCBs fields (i.e.
 * NULL and/or 0), except for p_pid which is incremented each time, and then
 * return a pointer to the removed element. PCBs get reused, so it is important
 * that no previous value persist in a PCB when it gets reallocated. */
static unsigned int next_pid = 1;
pcb_t *allocPcb() {
  if (list_empty(&pcbFree_h))
    return NULL;
  else {
    // extract the PCB pointed to by the node we are removing
    pcb_t *p = container_of(pcbFree_h.next, pcb_t, p_list);

    // remove the element from the pcbFree_h list
    list_del(pcbFree_h.next);

    // initialize list heads
    INIT_LIST_HEAD(&(p->p_list));
    INIT_LIST_HEAD(&(p->p_child));
    INIT_LIST_HEAD(&(p->p_sib));

    // reset pointers and scalar fields
    p->p_parent = NULL;
    p->p_time = 0;
    p->p_semAdd = NULL;
    p->p_supportStruct = NULL;
    p->p_prio = 0;

    // reset processor state fields
    p->p_s.cause = 0;
    p->p_s.entry_hi = 0;
    for (int i = 0; i < 32; i++) {
      p->p_s.gpr[i] = 0;
    }
    p->p_s.mie = 0;
    p->p_s.pc_epc = 0;
    p->p_s.status = 0;

    p->p_pid = next_pid;
    next_pid++;
    return p;
  }
}

/* This method is used to initialize a variable to be head pointer
 * to a process queue. */
void mkEmptyProcQ(struct list_head *head) {
  if (head != NULL)
    INIT_LIST_HEAD(head);
}

/* Return TRUE if the queue whose head is pointed to by head is empty.
 * Return FALSE otherwise. */
int emptyProcQ(struct list_head *head) {
  if (head == NULL)
    return FALSE;
  return list_empty(head);
}

/* Insert the PCB pointed by p into the process queue whose head pointer is
 * pointed to by head. The list must be ordered by priority (descending).
 * In case of equal priority, the new PCB must be inserted after the last PCB
 * with this priority (FIFO). */
void insertProcQ(struct list_head *head, pcb_t *p) {
  if (head == NULL || p == NULL)
    return;

  /* Iterate over the list and insert the element as soon as we find
   * an item with lower priority than the one being inserted.
   * list_add_tail inserts *before* the current iterator 'it'. */
  struct list_head *it;
  list_for_each(it, head) {
    pcb_t *item = container_of(it, pcb_t, p_list);
    if (p->p_prio > item->p_prio) {
      list_add_tail(&(p->p_list), it);
      return;
    }
  }

  /* If we reach here, the element has the lowest priority (or equal to the
   * last), so we add it to the tail of the list. */
  list_add_tail(&(p->p_list), head);
}

/* Return a pointer to the first PCB (i.e. the PCB with max priority)
 * from the process queue whose head is pointed to by head.
 * Do not remove this PCB from the process queue.
 * Return NULL if the process queue is empty. */
pcb_t *headProcQ(struct list_head *head) {
  if (head == NULL || emptyProcQ(head))
    return NULL;

  // Return the first element without removing it
  return container_of(head->next, pcb_t, p_list);
}

/* Return a pointer to the first PCB (i.e. the PCB with max priority)
 * from the process queue whose head is pointed to by head.
 * Return NULL if the process queue was initially empty; otherwise
 * return the pointer to the removed element. */
pcb_t *removeProcQ(struct list_head *head) {
  if (head == NULL || emptyProcQ(head))
    return NULL;

  // Remove and return the first element
  pcb_t *item = container_of(head->next, pcb_t, p_list);
  list_del(&(item->p_list));
  return item;
}

/* Remove the PCB pointed to by p from the process queue whose head pointer is
 * pointed to by head. If the desired entry is not in the indicated queue (an
 * error condition), return NULL; otherwise, return p. */
pcb_t *outProcQ(struct list_head *head, pcb_t *p) {
  if (head == NULL || p == NULL)
    return NULL;

  /* Sequential search for the element */
  struct list_head *it;
  list_for_each(it, head) {
    pcb_t *item = container_of(it, pcb_t, p_list);
    if (item == p) {
      list_del(&(p->p_list));
      return item;
    }
  }

  // Element not found in the queue
  return NULL;
}

/* Return TRUE if the PCB pointed to by p has no children.
 * Return FALSE otherwise. */
int emptyChild(pcb_t *p) {
  if (p == NULL)
    return FALSE;
  return list_empty(&(p->p_child));
}

/* Make the PCB pointed to by p a child of the PCB pointed to by prnt. */
void insertChild(pcb_t *prnt, pcb_t *p) {
  if (prnt == NULL || p == NULL || p->p_parent != NULL)
    return;

  // Update parent pointer
  p->p_parent = prnt;

  // Add p to the tail of parent's children list (siblings list)
  list_add_tail(&p->p_sib, &prnt->p_child);
}

/* Make the first child of the PCB pointed to by p no longer a child of p.
 * Return NULL if initially there were no children of p.
 * Otherwise, return a pointer to this removed first child PCB. */
pcb_t *removeChild(pcb_t *p) {
  if (p == NULL || list_empty(&p->p_child))
    return NULL;

  // Access the child to remove
  pcb_t *item = container_of(p->p_child.next, pcb_t, p_sib);

  list_del(p->p_child.next);
  item->p_parent = NULL;

  return item;
}

/* Make the PCB pointed to by p no longer the child of its parent.
 * If the PCB pointed to by p has no parent, return NULL;
 * otherwise, return p. Note that the element pointed to by p could be in an
 * arbitrary position (i.e. not be the first child of its parent). */
pcb_t *outChild(pcb_t *p) {
  if (p == NULL || p->p_parent == NULL)
    return NULL;

  // Remove child from siblings list
  list_del(&p->p_sib);
  p->p_parent = NULL;

  return p;
}
