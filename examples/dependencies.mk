foo: bar

bar: foobar

foobar: barfoo

barfoo: audio_playback_request

sleeping_state: &sleeping_request $battery $idle
	$sleeping_state = prolog(set_sleeping_state, &sleeping_request, \
                                 $battery, $idle)

cpu_frequency: sleeping_state &min_cpu_frequency &max_cpu_frequency \
			$battery $temperature
	$cpu_frequency = prolog(set_cpu_frequency, &min_cpu_frequency, &max_cpu_frequency)

audio_route: $current_profile $privacy_override $connected \
             $audio_active_policy_group
	$audio_route = prolog(set_routes)

audio_volume_limit: audio_route $audio_active_policy_group
	$volume_limit = prolog(set_volume_limits)

audio_cork: audio_route $audio_active_policy_group
	$audio_cork = prolog(set_corks)

audio_playback: cpu_frequency audio_route audio_volume_limit audio_cork \
                $cpu_load &audio_playback_request
	prolog(cpu_frequency_check, min, 300)
	prolog(cpu_load_check, max, 40)
#	$audio_playback = prolog(playback_request, \
#                                 &audio_playback_request.policy_group, \
#                                 &audio_playback_request.media)

audio_playback_request:
	dres(audio_playback, \
             &audio_playback_request = play, \
             &min_cpu_frequency = 300)
