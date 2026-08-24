/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   monitor.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 14:50:51 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/24 19:03:12 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	stop_sim(t_sim *sim)
{
	pthread_mutex_lock(&sim->stop_lock);
	sim->stop = 1;
	pthread_mutex_unlock(&sim->stop_lock);
	pthread_mutex_lock(&sim->arb_lock);
	pthread_cond_broadcast(&sim->arb_cond);
	pthread_mutex_unlock(&sim->arb_lock);
}

int	check_burnout(t_sim *sim, int i)
{
	long	last;
	long	now;

	pthread_mutex_lock(&sim->coders[i].state_lock);
	last = sim->coders[i].last_compile_start;
	pthread_mutex_unlock(&sim->coders[i].state_lock);
	now = now_ms();
	if (now - last <= sim->t_burnout)
		return (0);
	pthread_mutex_lock(&sim->log_lock);
	printf("%ld %d burned out\n", now - sim->start_ms, sim->coders[i].id);
	pthread_mutex_unlock(&sim->log_lock);
	stop_sim(sim);
	return (1);
}

int	all_done(t_sim *sim)
{
	int	i;
	int	done;

	if (sim->must_compile <= 0)
		return (0);
	i = 0;
	while (i < sim->n_coders)
	{
		pthread_mutex_lock(&sim->coders[i].state_lock);
		done = sim->coders[i].compiles_done;
		pthread_mutex_unlock(&sim->coders[i].state_lock);
		if (done < sim->must_compile)
			return (0);
		i++;
	}
	return (1);
}

void	*monitor_routine(void *arg)
{
	t_sim	*sim;
	int		i;

	sim = (t_sim *)arg;
	while (!sim_stopped(sim))
	{
		i = 0;
		while (i < sim->n_coders)
		{
			if (check_burnout(sim, i))
				return (NULL);
			i++;
		}
		if (all_done(sim))
			return (stop_sim(sim), NULL);
		usleep(500);
	}
	return (NULL);
}
