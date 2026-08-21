/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   log.c                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mide-fre <mide-fre@student.42porto.com>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/22 00:14:20 by mide-fre          #+#    #+#             */
/*   Updated: 2026/08/22 00:14:42 by mide-fre         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

void	log_state(t_coder *c, char *msg)
{
	pthread_mutex_lock(&c->sim->log_lock);
	if (!sim_stopped(c->sim))
		printf("%ld %d %s\n", elapsed(c->sim), c->id, msg);
	pthread_mutex_unlock(&c->sim->log_lock);
}
