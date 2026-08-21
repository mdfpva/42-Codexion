/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   codexion.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 15:46:35 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/21 18:01:26 by mide-fre         ###   ########.fr       */
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

#endif
