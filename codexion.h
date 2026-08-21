/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   codexion.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 15:46:35 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/22 00:33:39 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */
#ifndef CODEXION_H
# define CODEXION_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <pthread.h>
# include <sys/time.h>

# define FIFO 0
# define EDF 1

typedef struct s_sim	t_sim;

typedef struct s_request
{
	int		coder_id;
	long	deadline;
	long	seq;
	int		idx;
}	t_request;

typedef struct s_heap
{
	t_request	**items;
	int			size;
	int			capacity;
	int			policy;
}	t_heap;

typedef struct s_dongle
{
	int				id;
	int				available;
	long			available_at;
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	t_heap			queue;
}	t_dongle;

typedef struct s_coder
{
	int				id;
	pthread_t		thread;
	long			last_compile_start;
	int				compiles_done;
	pthread_mutex_t	state_lock;
	t_dongle		*first;
	t_dongle		*second;
	t_sim			*sim;
}	t_coder;

struct s_sim
{
	int				n_coders;
	long			t_burnout;
	long			t_compile;
	long			t_debug;
	long			t_refactor;
	int				must_compile;
	long			cooldown;
	int				policy;
	long			start_ms;
	int				stop;
	long			seq_counter;
	pthread_mutex_t	stop_lock;
	pthread_mutex_t	seq_lock;
	pthread_mutex_t	log_lock;
	t_dongle		*dongles;
	t_coder			*coders;
};

// HEAP
int			heap_init(t_heap *h, int capacity, int policy);
void		heap_destroy(t_heap *h);
int			heap_less(t_request *a, t_request *b, int policy);
void		heap_swap(t_heap *h, int i, int j);
t_request	*heap_peek(t_heap *h);
void		heap_up(t_heap *h, int i);
void		heap_down(t_heap *h, int i);
int			heap_push(t_heap *h, t_request *r);
t_request	*heap_pop(t_heap *h);
void		heap_remove(t_heap *h, t_request *r);

// TIME
long		now_ms(void);
long		elapsed(t_sim *sim);
int			sim_stopped(t_sim *sim);
long		next_seq(t_sim *sim);
long		coder_deadline(t_coder *c);

// LOG
void		log_state(t_coder *c, char *msg);

// DONGLE
void		ms_to_timespec(long target_ms, struct timespec *ts);
long		wake_time(t_dongle *d, long now);
int			can_take(t_dongle *d, t_request *req, long now);
int			dongle_acquire(t_coder *c, t_dongle *d);
void		dongle_release(t_dongle *d, t_sim *sim);
void		assign_dongles(t_sim *sim, int i);

#endif
