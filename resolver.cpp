#include "includes.h"

Resolver g_resolver{};;

CLagRecord* Resolver::FindLastRecord( Player* player )
{
    CLagRecord* record = nullptr;
    auto& records = LagCompensation->lag_records[ player->index( ) ];
    for ( auto it = records.rbegin( ); it != records.rend( ); it++ ) {
        if ( !LagCompensation->ValidRecord( &*it ) ) {
            if ( it->breaking_lag_comp )
                break;

            continue;
        }
        record = &*it;
    }

    return record;
}

CLagRecord* Resolver::FindFirstRecord( Player* player )
{
    CLagRecord* record = nullptr;
    auto& records = LagCompensation->lag_records[ player->index( ) ];
    for ( auto it = records.begin( ); it != records.end( ); it++ ) {
        if ( !LagCompensation->ValidRecord( &*it ) ) {
            if ( it->breaking_lag_comp || it->invalid )
                break;

            continue;
        }
        record = &*it;
    }

    return record;
}

float get_away_angle( CLagRecord* record )
{
    vec3_t pos;
    ang_t away;

    math::VectorAngles( g_cl.m_local->m_vecOrigin( ) - record->m_vecOrigin, away );
    return away.y;
}

bool is_yaw_sideways( float away, float yaw )
{
    const float delta =
        fabs( away - yaw );

    return ( delta > 25.f && delta < 165.f );
}

float anti_freestand( float at_target_angle, CLagRecord* record, float delta, ResolverHelper res_info )
{
    if ( !g_cl.m_weapon_info )
        return 6666.f;

    Player* player = record->m_player;

    auto eye_pos = player->GetShootPosition( );

    const float height = 64.f;

    vec3_t direction_1, direction_2;
    math::AngleVectors( ang_t( 0.f, at_target_angle - delta, 0.f ), &direction_1 );
    math::AngleVectors( ang_t( 0.f, at_target_angle + delta, 0.f ), &direction_2 );

    const auto left_eye_pos = eye_pos + vec3_t( 0, 0, height ) + ( direction_1 * 16.f );
    const auto right_eye_pos = eye_pos + vec3_t( 0, 0, height ) + ( direction_2 * 16.f );

    CGameTrace trace = { };
    CTraceFilterWorldOnly filter = { };

    g_csgo.m_engine_trace->TraceRay( Ray( left_eye_pos, eye_pos ), MASK_ALL, &filter, &trace );
    float left_fraction = trace.m_fraction;

    g_csgo.m_engine_trace->TraceRay( Ray( right_eye_pos, eye_pos ), MASK_ALL, &filter, &trace );
    float right_fraction = trace.m_fraction;

    penetration::PenetrationInput_t in;

    in.m_damage = height;
    in.m_target = player;
    in.m_from = g_cl.m_local;
    in.m_pos = left_eye_pos;

    penetration::PenetrationOutput_t out;
    if ( !penetration::run( &in, &out ) ) {
        if ( right_fraction < left_fraction ) {
            return at_target_angle + 90.f;
        }
        else if ( right_fraction > left_fraction ) {
            return at_target_angle - 90.f;
        }
        else if ( right_fraction == left_fraction ) {
            // middle.
            return at_target_angle + 180.f;
        }
        return 6666.f;
    }

    float left_damage = out.m_damage;

    in.m_damage = height;
    in.m_target = player;
    in.m_from = g_cl.m_local;
    in.m_pos = right_eye_pos;

    if ( !penetration::run( &in, &out ) ) {
        if ( right_fraction < left_fraction ) {
            return at_target_angle + 90.f;
        }
        else if ( right_fraction > left_fraction ) {
            return at_target_angle - 90.f;
        }
        else if ( right_fraction == left_fraction ) {
            // middle.
            return at_target_angle + 180.f;
        }
        return 6666.f;
    }

    float right_damage = out.m_damage;

    // 100% middle.
    if ( left_damage > 0 && right_damage > 0.f && right_fraction > 0.f && left_fraction > 0.f ) {
        if ( left_damage == right_damage && static_cast< int >( right_fraction ) == static_cast< int > ( left_fraction ) ) {
            // middle.
            return at_target_angle + 180.f;
        }
    }

    if ( left_damage <= 0 && right_damage <= 0 ) {
        if ( right_fraction < left_fraction ) {
            return at_target_angle + 90.f;
        }
        else if ( right_fraction > left_fraction ) {
            return at_target_angle - 90.f;
        }
        else if ( right_fraction == left_fraction ) {
            // middle.
            return at_target_angle + 180.f;
        }
    }
    else {
        if ( left_damage > right_damage ) {
            return at_target_angle - 90.f;
        }
        else if ( left_damage < right_damage ) {
            return at_target_angle + 90.f;
        }
        else {
            // middle.
            if ( left_damage == right_damage )
                return at_target_angle + 180.f;
        }
    }

    return 6666.f;
}

struct override_angle {
public:
    ang_t angle;
    vec3_t pos;
};

float override_resolver( CLagRecord* record, float away, ang_t viewangles, Player* player )
{
    vec3_t eye_pos = player->GetShootPosition( );

    std::vector< override_angle > angles;
    angles.push_back( { ang_t( 0.f, away - 180.f, 0.f ), vec3_t( ) } );
    angles.push_back( { ang_t( 0.f, away - 90.f, 0.f ), vec3_t( ) } );
    angles.push_back( { ang_t( 0.f, away + 90.f, 0.f ), vec3_t( ) } );
    angles.push_back( { ang_t( 0.f, away, 0.f ), vec3_t( ) } );

    vec3_t fwd, up;

    float best_fov = FLT_MAX;
    static float best_yaw = 0.f;
    int yaw_id = -1;

    const float range = 32.f;
    record->m_resolverMode += XOR( "override fallback[" );

    if ( g_input.GetKeyState( g_menu.main.aimbot.override_res_select.get( ) ) ) {
        for ( override_angle& angle : angles ) {
            math::AngleVectors( angle.angle, &angle.pos, &fwd, &up );
            angle.pos.normalized( );

            angle.pos *= range;

            angle.pos += eye_pos;

            float fov = math::GetFOV( viewangles, g_cl.m_shoot_pos, angle.pos );

            if ( fov < best_fov ) {
                best_yaw = angle.angle.y;
                best_fov = fov;
            }
        }
    }

    record->m_resolverMode += std::to_string( ( int )best_yaw );
    record->m_resolverMode += "]";

    return best_yaw;
}

void BruteforceResolver( CLagRecord* record, ResolverHelper& helper, float away, float back, float& delta_case_1, Player* player )
{
    //if ( helper.m_update_count <= 2 ) {
        const bool lastmove_valid = helper.m_walk_record.sim_time > 0.f;

        const auto delta_yaw = anti_freestand( away, record, 90.f, helper );

        if ( delta_yaw < 1337.f && std::fabs( math::AngleDiff( delta_yaw, helper.m_walk_record.lby ) ) < 35.f && lastmove_valid ) {
            player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
            record->m_resolverMode += XOR( "lastmove fallback" );
        }
        else if ( delta_yaw < 1337.f && std::fabs( math::AngleDiff( delta_yaw, record->m_flLowerBodyYawTarget ) ) > 60.f ) {
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
            record->m_resolverMode += XOR( "m_flLowerBodyYawTarget fallback" );
        }
        else {
            const auto lbydelta_yaw = anti_freestand( record->m_flLowerBodyYawTarget, record, std::fabs( helper.m_second_delta ), helper );

            if ( !is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) && !is_yaw_sideways( away, helper.m_walk_record.lby ) ) {
                player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
                record->m_resolverMode += XOR( "last fallback" );
            }
            else {
                const bool was_not_sideways_move = is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) && !is_yaw_sideways( away, helper.m_walk_record.lby );
                const bool is_not_sideways_at_all = !is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) && !is_yaw_sideways( away, helper.m_walk_record.lby );
                const float delta_yaw_move = ( std::fabs( math::AngleDiff( helper.m_walk_record.lby, delta_yaw ) ) );
                const float back_yaw_move = std::fabs( math::AngleDiff( helper.m_walk_record.lby, back ) );

                if ( was_not_sideways_move
                    || is_not_sideways_at_all
                    || delta_yaw_move > back_yaw_move ) {
                    player->m_angEyeAngles( ).y = back;
                    record->m_resolverMode += XOR( "back fallback" );
                }
                else {
                    player->m_angEyeAngles( ).y = delta_yaw;
                    record->m_resolverMode += XOR( "fs fallback" );
                }
            }
        
    //}
    //else {
        if ( std::abs( helper.m_second_delta ) > std::abs( helper.m_first_delta ) ) {
            record->m_resolverMode += XOR( "2:" );
            delta_case_1 = std::fabs( helper.m_second_delta );
        }
        else {
            record->m_resolverMode += XOR( "1:" );
            delta_case_1 = std::fabs( helper.m_first_delta );
        }

        const float delta_case = anti_freestand( record->m_flLowerBodyYawTarget, record, std::fabsf( delta_case_1 ), helper );

        if ( delta_case < 1337.f && math::AngleDiff( delta_case, record->m_flLowerBodyYawTarget ) > 65.f ) {
            player->m_angEyeAngles( ).y = delta_case;
            record->m_resolverMode += XOR( "auto fs fallback" );
        }
        else {
            const float fs_1_fallback_yaw = anti_freestand( back, record, 90.f, helper );

            if ( fs_1_fallback_yaw < 1337.f ) {
                player->m_angEyeAngles( ).y = fs_1_fallback_yaw;
                record->m_resolverMode += XOR( "auto fs[1] fallback" );
            }
            else {
                player->m_angEyeAngles( ).y = back;
                record->m_resolverMode += XOR( "auto fallback" );
            }

        }
    }
}

void ResolveStand( CLagRecord* record, ResolverHelper& helper, Player* player )
{
    const float away = get_away_angle( record );

    record->m_resolverMainMode = reso_main_modes::resolve_stand;

    static Player* best_player = nullptr;
    if ( Ragebot->resolver_override ) {
        auto targets = Ragebot->GetTargetList( );
        if ( !targets.empty( ) ) {
            ang_t viewangles = { };
            g_csgo.m_engine->GetViewAngles( viewangles );

            if ( g_input.GetKeyState( g_menu.main.aimbot.override_res_select_t.get( ) ) ) {
                float best_fov = FLT_MAX;
                for ( auto& target : targets ) {
                    if ( !target.target )
                        continue;

                    // get fov for this player
                    float fov = math::GetFOV( viewangles, g_cl.m_shoot_pos, target.target->m_vecOrigin( ) );

                    if ( fov < best_fov ) {
                        best_fov = fov;
                        best_player = target.target;
                    }
                }
            }

            if ( best_player ) {
                if ( best_player->dormant( ) || !best_player->alive( ) )
                    best_player = nullptr;
            }

            if ( !( !record || !record->m_player || record->m_player != best_player || !best_player ) ) {
                player->m_angEyeAngles( ).y = override_resolver( record, away, viewangles, player );
                helper.m_overrided = true;
                return;
            }
        }
    }
    else {
        // reset.
        best_player = nullptr;
    }

    helper.m_air_flick = 0.f;
    helper.m_moved = false;
    helper.m_air_caught_ground = false;

    if ( helper.m_walk_record.sim_time > 0.f ) {
        vec3_t delta = helper.m_walk_record.origin - record->m_vecOrigin;

        // check if moving record is close.
        if ( delta.length( ) <= 64.f ) {
            // indicate that we are using the moving m_flLowerBodyYawTarget.
            helper.m_moved = true;
        }
    }

    const float back = static_cast< float >( away + 180.f );
    const float move_lby_diff = math::AngleDiff( helper.m_walk_record.lby, record->m_flLowerBodyYawTarget );
    const float time_since_moving = g_csgo.m_globals->m_curtime - helper.m_walk_record.sim_time;
    const float override_backwards = 20.f;// g_menu.main.aimbot.override_backwards.get();

    if ( helper.m_walk_record.sim_time > 0.f ) {
        if ( record->prev_record ) {
            if ( !record->m_player->dormant( )
                && record->prev_record->m_player->dormant( )
                && ( record->m_vecOrigin - record->prev_record->m_vecOrigin ).length_2d( ) > 16.0f ) {
                helper.m_valid_run = false;
            }
        }
    }
    else {
        helper.m_valid_run = false;
    }

    bool no_update_at_all = ( time_since_moving < 0.22f && time_since_moving > 0.f );

    if ( helper.m_update_count > 0 )
        no_update_at_all = false;

    if ( !no_update_at_all )
        helper.m_valid_run = false;

    if ( helper.m_valid_run && helper.m_last_misses <= 0
        && ( std::fabs( move_lby_diff ) <= 20.f || is_yaw_sideways( away, helper.m_walk_record.lby ) ) ) {
        helper.m_body_flick = helper.m_walk_record.lby;
        player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
        record->m_resolverType = reso_modes::resolve_last_lby;
        record->m_resolverMode = XOR( "lastmove" );
        return;
    }

    //if (sequence_activity == act_csgo_idle_adjust_stoppedmoving || sequence_activity == act_csgo_idle_turn_balanceadjust) {
    //    if (record->animlayers[3].cycle > 0.8f)
    //    {
    //        record->m_resolverType = reso_modes::resolve_lby;
    //        record->m_resolverMode = XOR("m_flLowerBodyYawTarget");
    //        record->priority = priority_level::lby_flick;
    //        player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
    //        return;
    //    }
    //}

    const vec3_t current_origin = record->m_player->GetShootPosition( );

    // reset overlap delta amount
    helper.m_overlap_offset = 0.f;
    const float back_delta = math::AngleDiff( record->m_flLowerBodyYawTarget, back );
    const float forward_delta = math::AngleDiff( record->m_flLowerBodyYawTarget, static_cast< float >( back + 180.0f ) );
    const float freestand_angle = anti_freestand( away, record, 90.f, helper );

    const float freestand_delta = math::AngleDiff( record->m_flLowerBodyYawTarget, freestand_angle );

    const float abs_back_delta = std::fabs( back_delta );

    const float half_back_delta = ( abs_back_delta / 2.f );

    if ( half_back_delta >= 7.5f ) {
        if ( back_delta < 0.f )
            helper.m_overlap_offset = std::clamp( -half_back_delta, -45.f, 45.f );
        else
            helper.m_overlap_offset = std::clamp( half_back_delta, -45.f, 45.f );
    }

    const float min_body_yaw = 35.f;// g_menu.main.aimbot.body_yaw_delta.get();

    constexpr float max_last_lby_diff = 12.5f; // 5.f

    const bool freestand_close = std::fabsf( freestand_delta ) <= 15.f && freestand_angle < 1337.f;
    const bool back_close = ( ( std::fabsf( back_delta ) <= max_last_lby_diff ) || std::fabsf( back_delta ) <= helper.m_overlap_offset );
    const bool forward_close = std::fabsf( forward_delta ) <= max_last_lby_diff;

    int close_distance = 0;
    if ( ( back_close && freestand_close ) || ( forward_close && back_close ) || ( forward_close && freestand_close ) )
        close_distance = 0;
    else {
        if ( back_close )
            close_distance = 1;
        else if ( freestand_close ) {
            close_distance = 2;
        }
        else if ( forward_close && ( !freestand_close && !back_close ) ) {
            close_distance = 3;
        }
    }

    const bool has_close_angle = ( ( ( ( helper.m_update_count < 2 && helper.m_update_count > 0 ) )
        && close_distance > 0 ) );

    // bodyflick logic
    if ( helper.m_body_flick_miss <= 0 ) {
        if ( ( no_update_at_all || has_close_angle ) )
            // no update at all.. or if updated at the backward/forward/freestand angle..
        {
            helper.m_body_flick = record->m_flLowerBodyYawTarget;
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
            record->m_resolverType = reso_modes::resolve_stopped_moving;
            record->m_resolverMode = XOR( "body-flick" );

            if ( close_distance > 0 )
                record->m_resolverMode += XOR( "[" ) + std::to_string( close_distance ) + XOR( "]" );
            return;
        }
    }
    else {
        helper.m_body_flick = 0.f;
    }

    const float override_delta = static_cast< float >( back - override_backwards );

    if ( !helper.m_moved ) {
        const int missed_shots = helper.m_stand_no_move_idx;

        record->m_resolverType = reso_modes::resolve_no_data;
        helper.m_body_flick = 0.f;

        const float update_back_delta = math::AngleDiff( helper.m_body_flick, back );

        const bool update_back_close = ( ( std::fabsf( update_back_delta ) <= max_last_lby_diff ) ) && helper.m_update_count > 0;

        if ( ( no_update_at_all || has_close_angle ) && missed_shots <= 0 ) {
            record->m_resolverMode = XOR( "s:no upd" );
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
            return;
        }

        if ( update_back_close && missed_shots <= 0 ) {
            record->m_resolverMode = XOR( "s:lbyback" );
            player->m_angEyeAngles( ).y = back;
        }
        else {
            const float lby_test2_neg = record->m_flLowerBodyYawTarget - std::fabs( helper.m_first_delta );
            const float lby_test2_pos = record->m_flLowerBodyYawTarget + std::fabs( helper.m_first_delta );

            if ( lby_test2_neg != lby_test2_pos ) {
                record->m_resolverMode = XOR( "s:m_flLowerBodyYawTarget-delta" );
                player->m_angEyeAngles( ).y = lby_test2_pos < lby_test2_neg ? lby_test2_pos : lby_test2_pos;
            }
            else {
                const bool lby_fb = math::AngleDiff( back - 180.f, record->m_flLowerBodyYawTarget ) > 65.f && missed_shots < 2;
                const float freestand_yaw = anti_freestand( helper.m_body_flick, record, std::fabs( helper.m_second_delta ), helper );
                if ( ( ( ( helper.m_update_count > 0 && freestand_yaw < 1337.f ) && missed_shots <= 0 )
                    && math::AngleDiff( freestand_yaw, helper.m_first_delta ) < 35.f )
                    && !lby_fb ) {
                    record->m_resolverMode = XOR( "s:lbyfs" );
                    player->m_angEyeAngles( ).y = freestand_yaw;
                }
                else {
                    // rotate to forward.
                    const float rot_fs_yaw = anti_freestand( back - 180.f, record, std::fabs( lby_test2_neg ), helper );
                    const bool is_close_back = std::abs( math::AngleDiff( rot_fs_yaw, back ) ) <= 10.f;
                    if ( lby_fb && !is_close_back ) {
                        player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                        record->m_resolverMode += XOR( "s:m_flLowerBodyYawTarget fb" );
                    }
                    else {
                        if ( rot_fs_yaw < 1337.f ) {
                            record->m_resolverMode = XOR( "s:fs fallback" );
                            player->m_angEyeAngles( ).y = rot_fs_yaw;
                        }
                        else {
                            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                            record->m_resolverMode += XOR( "s:m_flLowerBodyYawTarget unk" );
                        }
                    }
                }
            }
        }
        helper.m_valid_run = false;
        return;
    }

    record->m_resolverMode = XOR( "m:" );
    record->m_resolverType = reso_modes::resolve_data;

    float delta_case_1 = FLT_MIN;
    const float last_lby_diff = math::AngleDiff( helper.m_side_last_body, record->m_flLowerBodyYawTarget );
    const bool delta_higher_min_body = std::fabs( helper.m_second_delta ) >= min_body_yaw;

    switch ( helper.m_stand_move_idx % 5 ) {
    case 0:
        if ( delta_higher_min_body ) {

            if ( helper.m_side_last_body == FLT_MIN
                || std::fabs( last_lby_diff ) > max_last_lby_diff ) {
                BruteforceResolver( record, helper, away, back, delta_case_1, player );
            }
            else {
                record->m_resolverMode += XOR( "last-diff" );
                player->m_angEyeAngles( ).y = helper.m_walk_record.lby + last_lby_diff;
            }
        }
        else {
            if ( helper.m_update_count > 0 && helper.m_update_count <= 1 ) {
                record->m_resolverMode += XOR( "m_flLowerBodyYawTarget" );
                player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;

            }
            else {
                record->m_resolverMode += XOR( "last" );
                player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
            }
        }

        break;
    case 1:
        if ( delta_higher_min_body ) {
            const auto last_yaw = anti_freestand( helper.m_walk_record.lby, record, 90.f, helper );

            if ( last_yaw < 1337.f && std::fabs( math::AngleDiff( record->m_flLowerBodyYawTarget, last_yaw ) ) <= 50.f && is_yaw_sideways( away, last_lby_diff ) && helper.m_valid_run ) {
                player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
                record->m_resolverMode += XOR( "last2" );
            }
            else {
                BruteforceResolver( record, helper, away, back, delta_case_1, player );
            }
        }
        else {
            record->m_resolverMode += helper.m_update_count > 0 ? XOR( "lby3" ) : XOR( "last3" );
            player->m_angEyeAngles( ).y = helper.m_update_count > 0 ? record->m_flLowerBodyYawTarget : helper.m_walk_record.lby;
        }

        break;
    case 2:
        if ( delta_higher_min_body ) {

            if ( helper.m_update_count > 0 && std::fabs( helper.m_second_delta ) <= 135.f ) { // 150.f
                record->m_resolverMode += XOR( "m_flLowerBodyYawTarget-delta2" );
                player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget + helper.m_second_delta;
            }
            else {
                record->m_resolverMode += XOR( "back" );
                player->m_angEyeAngles( ).y = back + helper.m_overlap_offset;
            }
        }
        else {
            const float lby_fs_crooked = anti_freestand( record->m_flLowerBodyYawTarget, record, math::AngleDiff( record->m_flLowerBodyYawTarget, helper.m_walk_record.lby ), helper );

            if ( lby_fs_crooked < 1337.7f ) {
                player->m_angEyeAngles( ).y = lby_fs_crooked;
                record->m_resolverMode += XOR( "auto fs[2] fallback" );
            }
            else {
                player->m_angEyeAngles( ).y = back;
                record->m_resolverMode += XOR( "auto fallback" );
            }
        }
        break;
    case 3:
        if ( is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) && !is_yaw_sideways( away, helper.m_walk_record.lby )
            || ( !is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) && !is_yaw_sideways( away, helper.m_walk_record.lby ) ) ) {
            player->m_angEyeAngles( ).y = away + 180.0f;
            record->m_resolverMode += XOR( "back fallback" );
        }
        else {
            record->m_resolverMode += XOR( "lby" );
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
        }
        break;
    default:
        break;
    }
}

bool caught_reso_ground( CLagRecord* record, ResolverHelper& helper, float at_target_backwards, float away, Player* player )
{
    if ( !helper.m_air_caught_ground )
        return false;

    record->m_resolverType = reso_main_modes::resolve_air;

    if ( helper.m_air_idx > 1 )
        return false;

    if ( record->prev_record->m_vecVelocity.length_2d( ) > 0.1f
        && std::fabs( math::AngleDiff( record->m_flLowerBodyYawTarget, record->prev_record->m_flLowerBodyYawTarget ) ) > 35.f ) {
        record->m_resolverMode = XOR( "air move-flick" );
        player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget; // body-flick
        return true;
    }

    //if (record->prev_record->eye_angles.y == record->prev_record->m_flLowerBodyYawTarget)
    //{
    //    record->priority = priority_level::no_updates_air;
    //    record->m_resolverMode = XOR("air record->prev_record flick");
    //    player->m_angEyeAngles( ).y = record->prev_record->m_flLowerBodyYawTarget; // body-flick
    //    return true;
    //}

    const float delta = math::AngleDiff( record->prev_record->m_flLowerBodyYawTarget, record->m_flLowerBodyYawTarget );
    const float abs_delta = std::fabs( delta );

    const bool delta_wasnt_too_far = abs_delta < 12.5f && abs_delta > 0.f;
    if ( delta_wasnt_too_far ) {
        // record->prev_record was backwards.
        if ( ( std::fabsf( math::AngleDiff( helper.m_air_flick, at_target_backwards ) ) < 20.f ) && !is_yaw_sideways( away, helper.m_air_flick ) ) {
            record->m_resolverMode = XOR( "air[7]" );
            player->m_angEyeAngles( ).y = at_target_backwards;
            return true;
        }
        else {
            const float delta_yaw = anti_freestand( helper.m_air_flick, record, helper.m_first_delta, helper );

            if ( std::fabsf( math::AngleDiff( helper.m_air_flick, delta_yaw ) ) < 10.f ) {
                record->m_resolverMode = XOR( "air[8]" );
                player->m_angEyeAngles( ).y = delta_yaw;
                return true;
            }
            else {
                record->m_resolverMode = XOR( "air[9]" );
                player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                return true;
            }
        }
    }
    else {
        // never updated
        if ( abs_delta <= 1.f ) {
            record->m_resolverMode = XOR( "air[11]" );
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
            return true;
        }
        else {
            // if delta was higher than update break, and updated to sideways.
            if ( abs_delta > 35.f && abs_delta < 65.f && is_yaw_sideways( away, record->m_flLowerBodyYawTarget ) ) {
                record->m_resolverMode = XOR( "air[10]" );
                player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                return true;
            }
        }
    }

    return false;
}

bool start( Player* player, CLagRecord* record, ResolverHelper& helper )
{
    auto state = player->m_PlayerAnimState( );
    if ( !state )
        return false;

    constexpr float max_last_lby_diff = 12.5f; // 5.f

    const auto diff = math::AngleDiff( record->m_flLowerBodyYawTarget, record->m_vecAbsAngles.y );
    helper.m_curr_sequence_activity = record->m_player->GetSequenceActivity( record->animlayers[ 3 ].m_sequence );
    helper.m_overrided = false;

    vec3_t velocity = record->m_vecVelocity;

    if ( !( player->m_fFlags( ) & FL_ONGROUND ) ) {
        if ( helper.m_networked ) {
            player->m_angEyeAngles( ).y = helper.m_net_angle;
        }
        else {
            if ( helper.m_curr_sequence_activity != 980 )
                helper.m_valid_run = false;

            record->m_resolverMainMode = reso_main_modes::resolve_air;

            // blabla do more stuff later here pls
            helper.m_moved = false;
            //helper.m_air_caught_ground = false;

            helper.m_air_flick = 0.f;

            // else run our matchmaking air resolver.

            // try to predict the direction of the player based on his velocity direction.
            // this should be a rough estimation of where he is looking.
            float vel_yaw = math::rad_to_deg( std::atan2( velocity.y, velocity.x ) );

            const float away = get_away_angle( record );

            float at_target_backwards = away + 180.f;
            float vel_yaw_backwards = vel_yaw + 180.f;

            float move_diff = math::AngleDiff( helper.m_walk_record.lby, record->m_flLowerBodyYawTarget );

            float back_diff = math::AngleDiff( at_target_backwards, record->m_flLowerBodyYawTarget );
            float vel_yaw_back_diff = math::AngleDiff( vel_yaw_backwards, record->m_flLowerBodyYawTarget );

            float back_diff_to_move = math::AngleDiff( at_target_backwards, helper.m_walk_record.lby );
            float vel_yaw_back_diff_to_move = math::AngleDiff( vel_yaw_backwards, helper.m_walk_record.lby );

            constexpr float low_neg_delta = 17.5f;
            constexpr float neg_delta = 35.f;

            // add a check like: if record->prev_record record has flicked before he was in air, use it, or else if record->prev_record record flicked to the same m_flLowerBodyYawTarget more than 2 times, use it.
            if ( record->prev_record ) {
                if ( record->prev_record->m_fFlags & FL_ONGROUND ) {
                    helper.m_air_caught_ground = true;
                    helper.m_air_flick = record->prev_record->m_flLowerBodyYawTarget;
                }

                if ( caught_reso_ground( record, helper, at_target_backwards, away, player ) )
                    return true;
            }

            if ( helper.m_update_count > 0 ) {

                //if (std::abs(diff) <= 15.f) {
                //    record->m_resolverType = reso_modes::resolve_air_flick;
                //    record->m_resolverMode = XOR("air[") + std::to_string(diff) + XOR("]");
                //    player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                //    return true;
                //}

                if ( helper.m_air_flick_miss > 0 ) {
                    if ( helper.m_flick_angle_missed > 0.f && math::AngleDiff( helper.m_flick_angle_missed, helper.m_body_flick ) > 35.f ) { // he broke m_flLowerBodyYawTarget + changed his angle
                        helper.m_air_flick_miss = 0; // reset counter.
                        helper.m_flick_angle_missed = 0.f;
                    }
                }
                else {
                    if ( math::AngleDiff( helper.m_body_flick, record->m_flLowerBodyYawTarget ) <= neg_delta ) {
                        record->m_resolverType = reso_modes::resolve_air_flick;
                        record->m_resolverMode = XOR( "air body-flick" );
                        player->m_angEyeAngles( ).y = helper.m_body_flick; // body-flick
                        return true;
                    }
                }
            }

            const bool move_good = move_diff <= low_neg_delta
                && helper.m_update_count <= 0
                && helper.m_air_idx < 1
                && helper.m_walk_record.sim_time > 0.f
                && move_diff < std::abs( back_diff_to_move )
                && move_diff <= helper.m_second_delta
                && helper.m_valid_run;

            if ( move_diff > low_neg_delta ) {
                helper.m_walk_record = move_data_t { };
            }
            else {
                if ( move_good ) // if lastmove difference is less than max m_flLowerBodyYawTarget break range / 2 then we can overlap. 
                {
                    record->m_resolverMode = XOR( "air-move" );
                    player->m_angEyeAngles( ).y = helper.m_walk_record.lby;
                    return true;
                }
            }

            // if missed less than 2 times.
            if ( helper.m_air_idx < 2 ) {
                const float delta_yaw = anti_freestand( away, record, helper.m_first_delta, helper );

                const float freestand_to_move_diff = std::fabs( math::AngleDiff( helper.m_walk_record.lby, delta_yaw ) );

                // if back and vel yaw difference is bigger than move diff and neg delta, most importantly, back diff isn't lower than freestand, cuz otherwise player is probably sideways.
                const bool too_high_delta_diff = ( ( back_diff >= neg_delta && vel_yaw_back_diff >= neg_delta )
                    && freestand_to_move_diff > back_diff )
                    // if back/vel yaw back difference is higher than move, and our delta is lower than move difference meaning we're probably not backwards.
                    || ( ( ( move_diff < back_diff && vel_yaw_back_diff < move_diff ) && helper.m_second_delta < move_diff )
                        && helper.m_second_delta != helper.m_first_delta );

                if ( too_high_delta_diff ) {
                    record->m_resolverMode = XOR( "air[1]" );
                    player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                    return true;
                }
                else {

                    if ( move_diff < freestand_to_move_diff ) {
                        record->m_resolverMode = XOR( "air[4]" );
                        player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;
                    }
                    else {
                        if ( ( back_diff_to_move > move_diff ) ) {
                            record->m_resolverMode = XOR( "air[6]" );
                            player->m_angEyeAngles( ).y = at_target_backwards;
                        }
                        else {
                            record->m_resolverMode = XOR( "air[5]" );
                            player->m_angEyeAngles( ).y = delta_yaw;
                        }
                    }
                }
                return true;
            }

            if ( back_diff <= vel_yaw_back_diff && fabs( vel_yaw_back_diff - low_neg_delta ) <= ( neg_delta + 10.f ) ) {
                player->m_angEyeAngles( ).y = vel_yaw_backwards;
                record->m_resolverMode = XOR( "air[2]" );
                return true;
            }

            if ( fabs( back_diff - neg_delta ) < ( neg_delta - 7.5f ) ) {
                record->m_resolverMode = XOR( "air[3]" );
                player->m_angEyeAngles( ).y = at_target_backwards;
            }
            else {
                player->m_angEyeAngles( ).y = vel_yaw_backwards;
                record->m_resolverMode = XOR( "air" );
            }
        }
        return true;
    }

    const bool moving = velocity.length_2d( ) > 0.1f/* && !record->fake_walking*/;

    if ( velocity.length_2d( ) > 35.f || !Ragebot->resolver_override ) {
        if ( moving ) {
            // apply m_flLowerBodyYawTarget to eyeangles.
            player->m_angEyeAngles( ).y = record->m_flLowerBodyYawTarget;

            helper.m_air_caught_ground = false;

            record->m_resolverMainMode = reso_main_modes::resolve_walk;

            helper.m_body_timer = g_csgo.m_globals->m_curtime + .22f;

            // reset stand and body index.
            if ( velocity.length_2d( ) >= 34.f/*record->max_speed * 0.20f*/ ) {
                helper.m_body_flick_miss = helper.m_failed_update_count = helper.m_last_misses
                    = helper.m_body_idx = helper.m_old_stand_move_idx = helper.m_old_stand_no_move_idx
                    = helper.m_stand_move_idx = helper.m_stand_no_move_idx = helper.m_air_idx = 0;
                helper.m_valid_run = true;

                if ( velocity.length_2d( ) > 75.f )
                    helper.m_update_count = 0;
            }
            //else {
            //    // resetting when velocity is getting less than 34...
            //    helper.m_valid_run = false;
            //}

            helper.m_air_flick = 0.f;
            helper.m_side_last_body = FLT_MIN;
            helper.m_overlap_offset = 0.f;

            if ( helper.m_update_count < 3 ) {
                helper.m_update_count = 0;
            }

            // copy the last record that this player was walking
            // we need it later on because it gives us crucial data.
            // copy move data over
            helper.m_walk_record.lby = record->m_flLowerBodyYawTarget;
            helper.m_walk_record.sim_time = record->m_flSimulationTime;
            helper.m_walk_record.origin = record->m_vecOrigin;
            helper.m_body_flick = record->m_flLowerBodyYawTarget;
            helper.m_flick_angle_missed = 0.f;

            record->m_resolverMode = XOR( "walk" );
            return true;
        }
    }

    if ( helper.m_curr_sequence_activity != 980 )
        helper.m_valid_run = false;

    if ( !helper.m_networked )
        ResolveStand( record, helper, player );
    else {
        player->m_angEyeAngles( ).y = helper.m_net_angle;
    }

    if ( helper.m_networked && !moving ) {
        record->m_resolverMode = XOR( "no fake" );
    }

    player->m_angEyeAngles( ).y = math::NormalizedAngle( player->m_angEyeAngles( ).y );
    return true;
}

bool ResolveLBY( CLagRecord* record, Player* player, ResolverHelper& res_info )
{
    if ( !record )
        return false;

    const bool passed_distance_check_prev = record->prev_record && abs( math::AngleDiff( record->m_flLowerBodyYawTarget, record->prev_record->m_flLowerBodyYawTarget ) ) > 35.0f;
    const bool passed_distance_check_normal = abs( math::AngleDiff( res_info.m_last_flicked_body, record->m_flLowerBodyYawTarget ) ) > 35.0f;

    const bool passed_distance_check = passed_distance_check_prev || passed_distance_check_normal;

    if ( g_csgo.m_globals->m_curtime > res_info.m_body_timer && res_info.m_flicked_anim ||
        passed_distance_check ) {
        res_info.m_body_timer = g_csgo.m_globals->m_curtime + 1.1f;
        res_info.m_flicked = true;
        res_info.m_last_flicked_body = record->m_flLowerBodyYawTarget;
        return true;
    }

    if ( g_csgo.m_globals->m_curtime > res_info.m_body_timer && !passed_distance_check )
        res_info.m_failed_update_count++;

    return false;
}

void Resolver::ResolveYaw( Player* player, CLagRecord* record )
{
    auto& helper = this->helper[ player->index( ) ];

    helper.m_valid = start( player, record, helper );

    //https://github.com/perilouswithadollarsign/cstrike15_src/blob/master/game/shared/cstrike15/csgo_playeranimstate.cpp#L2354
    // update LBY
    if ( !( player->m_fFlags( ) & FL_ONGROUND ) )
        return;

    // determine if should we resolve m_flLowerBodyYawTarget prediction or not.
    ResolveLBY( record, player, helper );

    if ( !helper.m_flicked )
        return;
        
    const float move_lby_diff = math::AngleDiff( helper.m_walk_record.lby, record->m_flLowerBodyYawTarget );

    if ( ( ( !helper.m_moved
        || std::fabs( helper.m_second_delta ) > 35.f
        || std::fabs( move_lby_diff ) <= 60.f ) ) && record->prev_record ) {

        if ( helper.m_curr_sequence_activity == 979
            && record->prev_record->animlayers[ 3 ].m_cycle <= 0.0f
            && record->prev_record->animlayers[ 3 ].m_weight <= 0.0f ) {
            helper.m_second_delta = std::fabs( helper.m_second_delta );
            helper.m_first_delta = std::fabs( helper.m_first_delta );
        }
        else {
            helper.m_second_delta = -std::fabs( helper.m_second_delta );
            helper.m_first_delta = -std::fabs( helper.m_first_delta );
        }
    }
    else {
        if ( helper.m_walk_record.sim_time <= 0.f ) {
            helper.m_second_delta = helper.m_second_delta;
            helper.m_side_last_body = FLT_MIN;
        }
        else {
            helper.m_first_delta = move_lby_diff;
            helper.m_second_delta = move_lby_diff;
            helper.m_side_last_body = std::fabs( move_lby_diff - 90.f ) <= 10.f ? FLT_MIN : record->m_flLowerBodyYawTarget;
        }
    }

    if ( helper.m_body_idx <= 2 ) {
        helper.m_body_flick_miss = 0;

        helper.m_body_flick = record->m_flLowerBodyYawTarget;
        record->m_resolverType = reso_modes::resolve_lby;
        record->m_resolverMode = XOR( "lbypred" );
        helper.m_update_count++;

        player->m_angEyeAngles( ).y = math::NormalizedAngle( record->m_flLowerBodyYawTarget );
    }
    else {
        helper.m_body_flick = 0.f;
    }

    helper.m_flicked_anim = false;
    helper.m_flicked = false;
}

//void on_fsn( )
//{
//    static bool should_clear = true;
//    if ( ctx.in_game ) {
//        should_clear = true;
//        return;
//    }
//
//    if ( should_clear ) {
//        for ( auto& i : info )
//            i.reset( );
//
//        should_clear = false;
//    }
//}