/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   codexion.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/21 15:46:35 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/21 23:49:17 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CODEXION_H
# define CODEXION_H

# include "stdio.h"
# include "stdlib.h"
# include "string.h"
# include "unistd.h"
# include "pthread.h"

typedef struct s_request {
	int		coder_id;
	long	deadline;   /* last_compile_start + time_to_burnout */
	long	seq;        /* ordem de chegada global */
}	t_request;

typedef struct s_dongle {
	int				id;
	int				available;
	long			available_at;   /* now + cooldown ao ser largado */
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	t_heap			queue;          /* pedidos pendentes */
}	t_dongle;

typedef struct s_coder {
	int				id;
	pthread_t		thread;
	long			last_compile_start;
	int				compiles_done;
	pthread_mutex_t	state_lock;     /* protege os dois campos acima */
	t_dongle		*first, *second;
	t_sim			*sim;
}	t_coder;

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

#endif
