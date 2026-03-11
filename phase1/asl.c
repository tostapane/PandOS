#include "./headers/asl.h"

/* Initialize the semdFree list to contain all the elements of the array static
 * semd_t semdTable[MAXPROC]. This method will be only called once during data
 * structure initialization. */
void initASL() {
  INIT_LIST_HEAD(&semdFree_h);
  INIT_LIST_HEAD(&semd_h);
  for (int i = 0; i < MAXPROC; i++) {
    list_add(&(semd_table[i].s_link), &semdFree_h);
  }
}

/* Helper function */
static semd_t *getSemd(int *key) {
  semd_t *iter;
  list_for_each_entry(iter, &semd_h, s_link) {
    if (iter->s_key == key)
      return iter;
    if (iter->s_key > key)
      return NULL; // Optimization
  }
  return NULL;
}

/* Insert the PCB pointed to by p at the tail of the process queue associated
 * with the semaphore whose key is semAdd and set the semaphore address of p to
 * semaphore with semAdd. If the semaphore is currently not active (i.e. there
 * is no descriptor for it in the ASL), allocate a new descriptor from the
 * semdFree list, insert it in the ASL (at the appropriate position), initialize
 * all of the fields (i.e. set s_key to semAdd, and s_procq to mkEmptyProcQ()),
 * and proceed as above. If a new semaphore descriptor needs to be allocated and
 * the semdFree list is empty, return TRUE. In all other cases return FALSE */
int insertBlocked(int *semAdd, pcb_t *p) {
  semd_t *sem_ptr = getSemd(semAdd);
  if (sem_ptr == NULL) {
    if (list_empty(&semdFree_h))
      return 1;
    struct list_head *readySem = list_next(&semdFree_h);
    list_del(readySem);
    semd_t *newSem = container_of(readySem, semd_t, s_link);
    newSem->s_key = semAdd;
    INIT_LIST_HEAD(&newSem->s_procq);
    semd_t *iter;
    struct list_head *insertPoint = &semd_h;
    list_for_each_entry(iter, &semd_h, s_link) {
      if (iter->s_key > semAdd) {
        insertPoint = &iter->s_link;
        break;
      }
    }
    list_add_tail(&newSem->s_link, insertPoint);
    sem_ptr = newSem;
  }
  p->p_semAdd = semAdd;
  list_add_tail(&p->p_list, &sem_ptr->s_procq);
  return 0;
}

/* Search the ASL for a descriptor of this semaphore. If none is found, return
 * NULL; other- wise, remove the first (i.e. head) PCB from the process queue of
 * the found semaphore de- scriptor and return a pointer to it. If the process
 * queue for this semaphore becomes empty (emptyProcQ(s_procq) is TRUE), remove
 * the semaphore descriptor from the ASL and return it to the semdFree list */
pcb_t *removeBlocked(int *semAdd) {
  semd_t *sem_ptr = getSemd(semAdd);
  if (sem_ptr == NULL || list_next(&sem_ptr->s_procq) == NULL)
    return NULL;
  struct list_head *p_link = list_next(&sem_ptr->s_procq);
  pcb_t *p = container_of(p_link, pcb_t, p_list);
  list_del(&p->p_list);
  if (list_empty(&sem_ptr->s_procq)) {
    list_del(&sem_ptr->s_link);
    list_add_tail(&sem_ptr->s_link, &semdFree_h);
  }
  return p;
}

/* Remove the PCB pointed to by p from the process queue associated with p’s
 * semaphore (p->p_semAdd) on the ASL. If PCB pointed to by p does not appear in
 * the process queue associated with p’s semaphore, which is an error condition,
 * return NULL; otherwise, return p.*/
pcb_t *outBlocked(pcb_t *p) {
  semd_t *sem_ptr = getSemd(p->p_semAdd);
  if (sem_ptr == NULL)
    return NULL;
  list_del(&p->p_list);
  if (list_empty(&sem_ptr->s_procq)) {
    list_del(&sem_ptr->s_link);
    list_add_tail(&sem_ptr->s_link, &semdFree_h);
  }
  p->p_semAdd = NULL;
  return p;
}

/* Return a pointer to the PCB that is at the head of the process queue
 * associated with the semaphore semAdd. Return NULL if semAdd is not found on
 * the ASL or if the process queue associated with semAdd is empty. */
pcb_t *headBlocked(int *semAdd) {
  semd_t *sem_ptr = getSemd(semAdd);
  if (sem_ptr == NULL || list_next(&sem_ptr->s_procq) == NULL)
    return NULL;
  struct list_head *p_link = list_next(&sem_ptr->s_procq);
  pcb_t *p = container_of(p_link, pcb_t, p_list);
  return p;
}
