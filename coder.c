/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   coder.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:35:45 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 17:26:20 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	precise_sleep(t_sim *sim, long ms)
{
	long	end;

	end = now_ms() + ms;
	while (now_ms() < end)
	{
		if (sim_stopped(sim))
			return ;
		usleep(200);
	}
}

void	take_single(t_coder *c)
{
	pthread_mutex_lock(&c->first->lock);
	c->first->available = 0;
	pthread_mutex_unlock(&c->first->lock);
	log_state(c, "has taken a dongle");
}

int	do_compile(t_coder *c)
{
	if (!dongle_acquire(c))
		return (0);
	pthread_mutex_lock(&c->state_lock);
	c->last_compile_start = now_ms();
	pthread_mutex_unlock(&c->state_lock);
	log_state(c, "is compiling");
	precise_sleep(c->sim, c->sim->t_compile);
	dongle_release(c->first, c->sim);
	dongle_release(c->second, c->sim);
	pthread_mutex_lock(&c->state_lock);
	c->compiles_done++;
	pthread_mutex_unlock(&c->state_lock);
	return (1);
}

void	*lone_coder(t_coder *c)
{
	take_single(c);
	while (!sim_stopped(c->sim))
		usleep(200);
	return (NULL);
}

void	*coder_routine(void *arg)
{
	t_coder	*c;

	c = (t_coder *)arg;
	if (c->sim->n_coders == 1)
		return (lone_coder(c));
	while (!sim_stopped(c->sim))
	{
		if (!do_compile(c))
			break ;
		log_state(c, "is debugging");
		precise_sleep(c->sim, c->sim->t_debug);
		log_state(c, "is refactoring");
		precise_sleep(c->sim, c->sim->t_refactor);
	}
	return (NULL);
}
