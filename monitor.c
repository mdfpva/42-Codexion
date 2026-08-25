/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   monitor.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 14:50:51 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/25 11:22:51 by mide-fre         ###   ########.fr       */
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

void	monitor_wait(t_sim *sim, long *next)
{
	struct timespec	ts;

	*next += 1;
	ms_to_timespec(*next, &ts);
	pthread_mutex_lock(&sim->arb_lock);
	pthread_cond_timedwait(&sim->arb_cond, &sim->arb_lock, &ts);
	pthread_mutex_unlock(&sim->arb_lock);
}

long	now_mono(void)
{
	struct timespec	ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void	*monitor_routine(void *arg)
{
	t_sim	*sim;
	int		i;
	long	t0;
	long	m0;
	long	prev;
	long	next;

	sim = (t_sim *)arg;
	prev = now_ms();
	next = now_ms();
	while (!sim_stopped(sim))
	{
		if (now_ms() - prev > 10)
			fprintf(stderr, "GAP %ld at %ld\n",
				now_ms() - prev, elapsed(sim));
		i = 0;
		while (i < sim->n_coders)
		{
			t0 = now_ms();
			if (check_burnout(sim, i))
				return (NULL);
			if (now_ms() - t0 > 5)
				fprintf(stderr, "SLOW check %d: %ld at %ld\n",
					i + 1, now_ms() - t0, elapsed(sim));
			i++;
		}
		if (all_done(sim))
			return (stop_sim(sim), NULL);
		t0 = now_ms();
		m0 = now_mono();
		monitor_wait(sim, &next);
		if (now_ms() - t0 > 10)
			fprintf(stderr, "SLOW wall=%ld mono=%ld at %ld\n",
				now_ms() - t0, now_mono() -m0, elapsed(sim));
		prev = now_ms();
	}
	return (NULL);
}
