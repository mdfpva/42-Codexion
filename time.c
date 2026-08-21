/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   time.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:09:09 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/22 00:09:36 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

long	now_ms(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return ((long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

long	elapsed(t_sim *sim)
{
	return (now_ms() - sim->start_ms);
}

int	sim_stopped(t_sim *sim)
{
	int	s;

	pthread_mutex_lock(&sim->stop_lock);
	s = sim->stop;
	pthread_mutex_unlock(&sim->stop_lock);
	return (s);
}

long	next_seq(t_sim *sim)
{
	long	s;

	pthread_mutex_lock(&sim->seq_lock);
	s = sim->seq_counter++;
	pthread_mutex_unlock(&sim->seq_lock);
	return (s);
}

long	coder_deadline(t_coder *c)
{
	long	d;

	pthread_mutex_lock(&c->state_lock);
	d = c->last_compile_start + c->sim->t_burnout;
	pthread_mutex_unlock(&c->state_lock);
	return (d);
}
