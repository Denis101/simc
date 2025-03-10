// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "unique_gear_dragonflight.hpp"

#include "action/dot.hpp"
#include "actor_target_data.hpp"
#include "buff/buff.hpp"
#include "darkmoon_deck.hpp"
#include "dbc/item_database.hpp"
#include "ground_aoe.hpp"
#include "item/item.hpp"
#include "item/item_targetdata_initializer.hpp"
#include "set_bonus.hpp"
#include "player/action_priority_list.hpp"
#include "player/action_variable.hpp"
#include "player/pet.hpp"
#include "sim/cooldown.hpp"
#include "sim/sim.hpp"
#include "stats.hpp"
#include "unique_gear.hpp"
#include "unique_gear_helper.hpp"

namespace unique_gear::dragonflight
{
namespace consumables
{
// TODO: implement reasonable model of losing buff due to damage taken. note that 'rotting from within' randomly procced
// from using toxic phial/potions will eventually cause you to lose the buff, even if you are taking 0 external damage.
void iced_phial_of_corrupting_rage( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, "iced_phial_of_corrupting_rage" );
  if ( !buff )
  {
    auto crit = make_buff<stat_buff_t>( effect.player, "corrupting_rage", effect.player->find_spell( 374002 ) )
                    ->add_stat( STAT_CRIT_RATING, effect.driver()->effectN( 2 ).average( effect.item ) )
                    ->set_quiet( false );

    buff = make_buff( effect.player, "iced_phial_of_corrupting_rage", effect.driver() )
      ->set_cooldown( 0_ms )
      ->set_chance( 1.0 )
      ->set_stack_change_callback( [ crit ]( buff_t*, int, int new_ ) {
        if ( new_ )
          crit->trigger();
        else
          crit->expire();
      } );

    auto debuff = make_buff( effect.player, "overwhelming_rage", effect.player->find_spell( 374037 ) )
      ->set_stack_change_callback( [ buff, crit ]( buff_t*, int, int new_ ) {
        if ( !new_ && buff->check() )
          crit->trigger();
      } );

    crit->set_stack_change_callback( [ buff, debuff ]( buff_t*, int, int new_ ) {
      if ( !new_ && buff->check() )
        debuff->trigger();
    } );

    if ( effect.player->sim->dragonflight_opts.corrupting_rage_uptime < 1 )
    {
      crit->set_constant_behavior( buff_constant_behavior::NEVER_CONSTANT );
      effect.player->register_combat_begin( [ crit, debuff ]( player_t* ) {
        make_repeating_event( crit->player->sim, 1_s, [ crit, debuff ]() {
          double debuff_uptime = 1 - crit->player->sim->dragonflight_opts.corrupting_rage_uptime;
          double disable_chance = debuff_uptime / ( debuff->buff_duration() - debuff->buff_duration() * debuff_uptime ).total_seconds();
          if ( crit->rng().roll( disable_chance ) )
            crit->expire();
        } );
      } );
    }
  }

  effect.custom_buff = buff;
}

// TODO: implement reasonable model of losing buff due to other players coming too close. note that
// driver()->effectN(2)->trigger() is called every 1s to check within a 10yd radius for close players.
void phial_of_charged_isolation( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, effect.name() );
  if ( !buff )
  {
    auto amount = effect.driver()->effectN( 1 ).average( effect.item );
    auto linger_mul = effect.driver()->effectN( 2 ).percent();

    auto stat_buff =
        make_buff<stat_buff_t>( effect.player, "phial_of_charged_isolation_stats", effect.player->find_spell( 371387 ) )
            ->add_stat_from_effect( 1, amount );

    auto linger_buff =
        make_buff<stat_buff_t>( effect.player, "phial_of_charged_isolation_linger", effect.player->find_spell( 384713 ) )
            ->add_stat_from_effect( 1, amount * linger_mul );

    buff = make_buff( effect.player, effect.name(), effect.driver() )
      ->set_stack_change_callback( [ stat_buff ]( buff_t*, int, int new_ ) {
        if ( new_ )
          stat_buff->trigger();
        else
          stat_buff->expire();
      } );

    stat_buff->set_stack_change_callback( [ buff, linger_buff ]( buff_t*, int, int new_ ) {
      if ( !new_ && buff->check() )
        linger_buff->trigger();
    } );
  }

  effect.custom_buff = buff;
}

void phial_of_elemental_chaos( special_effect_t& effect )
{
  if ( create_fallback_buffs( effect, { "elemental_chaos_fire", "elemental_chaos_air", "elemental_chaos_earth",
                                        "elemental_chaos_frost" } ) )
  {
    return;
  }

  auto buff = buff_t::find( effect.player, effect.name() );
  if ( !buff )
  {
    std::vector<buff_t*> buff_list;
    auto amount = effect.driver()->effectN( 1 ).average( effect.item );
    auto duration = effect.driver()->effectN( 1 ).period();

    effect.player->buffs.elemental_chaos_fire = buff_list.emplace_back(
        make_buff<stat_buff_t>( effect.player, "elemental_chaos_fire", effect.player->find_spell( 371348 ) )
            ->add_stat( STAT_CRIT_RATING, amount )
            ->set_default_value_from_effect_type( A_MOD_CRIT_DAMAGE_BONUS )
            ->set_duration( duration ) );
    effect.player->buffs.elemental_chaos_air = buff_list.emplace_back(
        make_buff<stat_buff_t>( effect.player, "elemental_chaos_air", effect.player->find_spell( 371350 ) )
            ->add_stat( STAT_HASTE_RATING, amount )
            ->set_default_value_from_effect_type( A_MOD_SPEED_ALWAYS )
            ->add_invalidate( CACHE_RUN_SPEED )
            ->set_duration( duration ) );
    effect.player->buffs.elemental_chaos_earth = buff_list.emplace_back(
        make_buff<stat_buff_t>( effect.player, "elemental_chaos_earth", effect.player->find_spell( 371351 ) )
            ->add_stat( STAT_MASTERY_RATING, amount )
            ->set_default_value_from_effect_type( A_MOD_DAMAGE_PERCENT_TAKEN )
            ->set_duration( duration ) );
    effect.player->buffs.elemental_chaos_frost = buff_list.emplace_back(
        make_buff<stat_buff_t>( effect.player, "elemental_chaos_frost", effect.player->find_spell( 371353 ) )
            ->add_stat( STAT_VERSATILITY_RATING, amount )
            ->set_default_value_from_effect_type( A_MOD_CRITICAL_HEALING_AMOUNT )
            ->set_duration( duration ) );

    buff = make_buff( effect.player, effect.name(), effect.driver() )
      ->set_tick_callback( [ buff_list ]( buff_t* b, int, timespan_t ) {
        buff_list[ b->rng().range( buff_list.size() ) ]->trigger();
      } );
  }

  effect.custom_buff = buff;
}

void phial_of_glacial_fury( special_effect_t& effect )
{
  // Damage proc
  struct glacial_fury_t : public generic_aoe_proc_t
  {
    const buff_t* buff;
    glacial_fury_t( const special_effect_t& e, const buff_t* b )
      : generic_aoe_proc_t( e, "glacial_fury", e.driver()->effectN( 2 ).trigger(), true ), buff( b )
    {
      base_dd_min = base_dd_max = e.driver()->effectN( 3 ).average( e.item );
    }

    double composite_aoe_multiplier( const action_state_t* s ) const override
    {
      // spell data is flagged to ignore all caster multpliers, so da_multiplier is not snapshot. however, the 15% per
      // buff does take effect and as this is an aoe action, we can use composite_aoe_multiplier without having  to
      // adjust snapshot flags.
      return generic_aoe_proc_t::composite_aoe_multiplier( s ) * ( 1.0 + buff->check_stack_value() );
    }
  };

  // Buff that triggers when you hit a new target
  auto new_target_buff = create_buff<buff_t>( effect.player, effect.player->find_spell( 373265 ) );
  new_target_buff->set_default_value( effect.driver()->effectN( 4 ).percent() );

  // effect to trigger damage proc
  auto damage_effect            = new special_effect_t( effect.player );
  damage_effect->type           = SPECIAL_EFFECT_EQUIP;
  damage_effect->source         = SPECIAL_EFFECT_SOURCE_ITEM;
  damage_effect->spell_id       = effect.spell_id;
  damage_effect->cooldown_      = 0_ms;
  damage_effect->execute_action = create_proc_action<glacial_fury_t>( "glacial_fury", effect, new_target_buff );
  effect.player->special_effects.push_back( damage_effect );

  // callback to proc damage
  auto damage_cb = new dbc_proc_callback_t( effect.player, *damage_effect );
  damage_cb->initialize();
  damage_cb->deactivate();

  // effect to trigger new target buff
  auto new_target_effect          = new special_effect_t( effect.player );
  new_target_effect->name_str     = "glacial_fury_new_target";
  new_target_effect->type         = SPECIAL_EFFECT_EQUIP;
  new_target_effect->source       = SPECIAL_EFFECT_SOURCE_ITEM;
  new_target_effect->proc_chance_ = 1.0;
  new_target_effect->proc_flags_  = PF_ALL_DAMAGE | PF_PERIODIC;
  new_target_effect->proc_flags2_ = PF2_ALL_HIT | PF2_PERIODIC_DAMAGE;
  new_target_effect->custom_buff  = new_target_buff;
  effect.player->special_effects.push_back( new_target_effect );

  // callback to trigger buff on attacking a new target
  struct glacial_fury_buff_cb_t : public dbc_proc_callback_t
  {
    std::vector<int> target_list;

    glacial_fury_buff_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e ) {}

    void execute( action_t* a, action_state_t* s ) override
    {
      if ( !a->harmful )
        return;

      if ( range::contains( target_list, s->target->actor_spawn_index ) )
        return;

      dbc_proc_callback_t::execute( a, s );
      target_list.push_back( s->target->actor_spawn_index );
    }

    void reset() override
    {
      dbc_proc_callback_t::reset();
      target_list.clear();
    }
  };

  auto new_target_cb = new glacial_fury_buff_cb_t( *new_target_effect );
  new_target_cb->initialize();
  new_target_cb->deactivate();

  // Phial buff itself
  auto buff = create_buff<buff_t>( effect.player, effect.driver() );
  buff->set_chance( 1.0 )
      ->set_stack_change_callback( [ damage_cb, new_target_cb ]( buff_t*, int, int new_ ) {
        if ( new_ )
        {
          damage_cb->activate();
          new_target_cb->activate();
        }
        else
        {
          damage_cb->deactivate();
          new_target_cb->deactivate();
        }
      } );

  effect.custom_buff = buff;
}

// TODO: implement reasonable model of losing buff due to movement
void phial_of_static_empowerment( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, effect.name() );
  if ( !buff )
  {
    auto primary = make_buff<stat_buff_t>( effect.player, "static_empowerment", effect.player->find_spell( 370772 ) );
    primary->add_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect.item ) / primary->max_stack() );
    primary->set_constant_behavior( buff_constant_behavior::NEVER_CONSTANT );

    buff = make_buff( effect.player, effect.name(), effect.driver() )
      ->set_stack_change_callback( [ primary ]( buff_t*, int, int new_ ) {
        if ( new_ )
          primary->trigger();
        else
          primary->expire();
      } );

    auto speed = make_buff<stat_buff_t>( effect.player, "mobile_empowerment", effect.player->find_spell( 370773 ) )
      ->add_stat( STAT_SPEED_RATING, effect.driver()->effectN( 2 ).average( effect.item ) )
      ->set_stack_change_callback( [ buff, primary ]( buff_t*, int, int new_ ) {
        if ( !new_ && buff->check() )
          primary->trigger();
      } );

    primary->set_stack_change_callback( [ buff, speed ]( buff_t*, int, int new_ ) {
      if ( !new_ && buff->check() )
        speed->trigger();
    } );

    effect.player->buffs.static_empowerment = primary;
  }

  effect.custom_buff = buff;
}

void bottled_putrescence( special_effect_t& effect )
{
  struct bottled_putrescence_t : public proc_spell_t
  {
    struct bottled_putrescence_tick_t : public proc_spell_t
    {
      bottled_putrescence_tick_t( const special_effect_t& e )
        : proc_spell_t( "bottled_putrescence_tick", e.player, e.player->find_spell( 371993 ) )
      {
        dual = ground_aoe = true;
        aoe = -1;
        // every tick damage is rounded down
        base_dd_min = base_dd_max = util::floor( e.driver()->effectN( 1 ).average( e.item ) );
      }

      result_amount_type amount_type( const action_state_t*, bool ) const override
      {
        return result_amount_type::DMG_OVER_TIME;
      }
    };

    action_t* damage;
    timespan_t duration;

    bottled_putrescence_t( const special_effect_t& e )
      : proc_spell_t( "bottled_putrescence", e.player, e.driver() ),
        damage( create_proc_action<bottled_putrescence_tick_t>( "bottled_putresence", e ) ),
        duration( data().effectN( 1 ).trigger()->duration() * inhibitor_mul( e.player ) )
    {
      damage->stats = stats;
    }

    void impact( action_state_t* s ) override
    {
      proc_spell_t::impact( s );

      make_event<ground_aoe_event_t>( *sim, player, ground_aoe_params_t()
        .target( s->target )
        .pulse_time( damage->base_tick_time )
        .duration( duration - 1_ms )  // has 0tick, but no tick at the end
        .action( damage ), true );
    }
  };

  effect.execute_action = create_proc_action<bottled_putrescence_t>( "bottled_putrescence", effect );
}

void chilled_clarity( special_effect_t& effect )
{
  if ( create_fallback_buffs( effect, { "potion_of_chilled_clarity" } ) )
    return;

  auto buff = create_buff<buff_t>( effect.player, effect.trigger() );
  buff->set_default_value_from_effect_type( A_355 )
      ->set_duration( timespan_t::from_seconds( effect.driver()->effectN( 1 ).base_value() ) )
      ->set_duration_multiplier( inhibitor_mul( effect.player ) );

  effect.custom_buff = effect.player->buffs.chilled_clarity = buff;
}

void elemental_power( special_effect_t& effect )
{
  effect.duration_ = effect.duration() * inhibitor_mul( effect.player );
}

void shocking_disclosure( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, "shocking_disclosure" );
  if ( !buff )
  {
    auto damage =
      create_proc_action<generic_aoe_proc_t>( "shocking_disclosure", effect, "shocking_disclosure", effect.trigger() );
    damage->split_aoe_damage = false;
    damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect.item );
    damage->dual = true;

    if ( auto pot_action = effect.player->find_action( "potion" ) )
      damage->stats = pot_action->stats;

    buff = make_buff( effect.player, "shocking_disclosure", effect.driver() )
      ->set_duration_multiplier( inhibitor_mul( effect.player ) )
      ->set_tick_callback( [ damage ]( buff_t* b, int, timespan_t ) {
        damage->execute_on_target( b->player->target );
      } );
  }

  effect.custom_buff = buff;
}
}  // namespace consumables

namespace enchants
{
custom_cb_t writ_enchant( stat_e stat )
{
  return [ stat ]( special_effect_t& effect ) {
    auto amount = effect.driver()->effectN( 1 ).average( effect.player );

    auto new_driver = effect.trigger();
    auto new_trigger = new_driver->effectN( 1 ).trigger();

    std::string buff_name = new_trigger->name_cstr();
    util::tokenize( buff_name );
    auto buff = debug_cast<stat_buff_t*>( buff_t::find( effect.player, buff_name ) );
    auto buff_stat = STAT_NONE;

    if ( stat == STAT_NONE )
    {
      buff_stat = util::translate_rating_mod( new_trigger->effectN( 1 ).misc_value1() );
    }
    else
    {
      buff_stat = effect.player->convert_hybrid_stat( stat );
    }

    if ( buff == nullptr )
    {
      buff = create_buff<stat_buff_t>( effect.player, buff_name, new_trigger );
    }

    buff->add_stat( buff_stat, amount );

    effect.name_str = buff_name;
    effect.custom_buff = buff;
    effect.spell_id = new_driver->id();

    new dbc_proc_callback_t( effect.player, effect );
  };
}

void frozen_devotion( special_effect_t& effect )
{
  auto new_driver = effect.driver()->effectN( 1 ).trigger();
  auto proc = create_proc_action<generic_aoe_proc_t>( "frozen_devotion", effect, "frozen_devotion",
                                                      new_driver->effectN( 1 ).trigger(), true );

  proc->base_dd_min += effect.driver()->effectN( 1 ).average( effect.player );
  proc->base_dd_max += effect.driver()->effectN( 1 ).average( effect.player );
  
  effect.name_str = new_driver->name_cstr();
  effect.spell_id = new_driver->id();
  effect.execute_action = proc;

  new dbc_proc_callback_t( effect.player, effect );
}

void wafting_devotion( special_effect_t& effect )
{
  auto new_driver = effect.trigger();
  auto new_trigger = new_driver->effectN( 1 ).trigger();

  auto haste = effect.driver()->effectN( 1 ).average( effect.player );
  auto speed = effect.driver()->effectN( 2 ).average( effect.player );

  std::string buff_name = new_trigger->name_cstr();
  util::tokenize( buff_name );

  auto buff = debug_cast<stat_buff_t*>( buff_t::find( effect.player, buff_name ) );
  if ( buff == nullptr )
  {
    buff = create_buff<stat_buff_t>( effect.player, buff_name, new_trigger );
  }
  
  buff->set_stat( STAT_HASTE_RATING, haste )
      ->add_stat( STAT_SPEED_RATING, speed );
  
  effect.name_str = buff_name;
  effect.custom_buff = buff;
  effect.spell_id = new_driver->id();
  effect.trigger_spell_id = new_trigger->id();

  new dbc_proc_callback_t( effect.player, effect );
}

void completely_safe_rockets( special_effect_t& effect )
{

  auto missile = effect.trigger();
  auto blast = missile -> effectN( 1 ).trigger();

  auto blast_proc = create_proc_action<generic_aoe_proc_t>( "completely_safe_rocket_blast", effect, "completely_safe_rocket_blast", blast, true );
  blast_proc -> base_dd_min = blast_proc -> base_dd_max = effect.driver() -> effectN( 1 ).average( effect.player );
  blast_proc -> travel_speed = missile -> missile_speed();

  effect.execute_action = blast_proc;
  effect.proc_flags2_ = PF2_ALL_HIT | PF2_PERIODIC_DAMAGE;

  new dbc_proc_callback_t( effect.player, effect );
}

void high_intensity_thermal_scanner( special_effect_t& effect )
{
  double value = effect.driver()->effectN( 1 ).average( effect.player );

  auto crit_buff = create_buff<stat_buff_t>( effect.player, "high_intensity_thermal_scanner_crit", effect.trigger() );
  crit_buff->add_stat( STAT_CRIT_RATING, value );

  auto vers_buff = create_buff<stat_buff_t>( effect.player, "high_intensity_thermal_scanner_vers", effect.trigger() );
  vers_buff->add_stat( STAT_VERSATILITY_RATING, value );

  auto haste_buff = create_buff<stat_buff_t>( effect.player, "high_intensity_thermal_scanner_haste", effect.trigger() );
  haste_buff->add_stat( STAT_HASTE_RATING, value );

  auto mastery_buff = create_buff<stat_buff_t>( effect.player, "high_intensity_thermal_scanner_mastery", effect.trigger() );
  mastery_buff->add_stat( STAT_MASTERY_RATING, value );

  struct buff_cb_t : public dbc_proc_callback_t
  {
    buff_t* crit;
    buff_t* vers;
    buff_t* haste;
    buff_t* mastery;

    buff_cb_t( const special_effect_t& e, buff_t* crit, buff_t* vers, buff_t* haste, buff_t* mastery ) : dbc_proc_callback_t( e.player, e ),
      crit( crit ), vers( vers ), haste( haste ), mastery( mastery )
    {}

    void execute( action_t*, action_state_t* s ) override
    {
      switch (s -> target -> race)
      {
      case RACE_MECHANICAL:
      case RACE_DRAGONKIN:
        mastery->trigger();
        break;
      case RACE_ELEMENTAL:
      case RACE_BEAST:
        crit->trigger();
        break;
      case RACE_UNDEAD:
      case RACE_HUMANOID:
        vers->trigger();
        break;
      case RACE_GIANT:
      case RACE_DEMON:
      case RACE_ABERRATION:
      default:
        haste->trigger();
        break;
      }
    }
  };

  new buff_cb_t( effect, crit_buff, vers_buff, haste_buff, mastery_buff );
}

void gyroscopic_kaleidoscope( special_effect_t& effect )
{
  int buff_id;
  switch ( effect.driver() -> id() )
  {
  case 385892:
    buff_id = 385891;
    break;
  case 385886:
    buff_id = 385885;
    break;
  case 385765:
  default:
    buff_id = 385860;
    break;
  }

  auto buff_data = effect.player -> find_spell( buff_id );
  double stat = buff_data -> effectN( 1 ).average( effect.player );

  auto buff = create_buff<stat_buff_t>( effect.player, "gyroscopic_kaleidoscope", buff_data );
  buff -> add_stat( util::parse_stat_type( effect.player -> dragonflight_opts.gyroscopic_kaleidoscope_stat ), stat );

  effect.player->register_combat_begin( [ buff ]( player_t* ) {
    buff -> trigger();
    make_repeating_event( buff -> player -> sim, buff -> buff_duration() , [ buff ]() { buff -> trigger(); } );
  } );
}

void projectile_propulsion_pinion( special_effect_t& effect )
{
  auto buff_data = effect.player -> find_spell( 385942 );
  double value = effect.driver()->effectN( 1 ).average( effect.player ) / 2;

  auto crit_buff = create_buff<stat_buff_t>( effect.player, "projectile_propulsion_pinion_crit", buff_data );
  crit_buff->add_stat( STAT_CRIT_RATING, value );

  auto vers_buff = create_buff<stat_buff_t>( effect.player, "projectile_propulsion_pinion_vers", buff_data );
  vers_buff->add_stat( STAT_VERSATILITY_RATING, value );

  auto haste_buff = create_buff<stat_buff_t>( effect.player, "projectile_propulsion_pinion_haste", buff_data );
  haste_buff->add_stat( STAT_HASTE_RATING, value );

  auto mastery_buff = create_buff<stat_buff_t>( effect.player, "projectile_propulsion_pinion_mastery", buff_data );
  mastery_buff->add_stat( STAT_MASTERY_RATING, value );

  std::map<stat_e, stat_buff_t*> buffs = {
    { STAT_CRIT_RATING, crit_buff },
    { STAT_VERSATILITY_RATING, vers_buff },
    { STAT_HASTE_RATING, haste_buff },
    { STAT_MASTERY_RATING, mastery_buff },
  };

  effect.player->register_combat_begin( [ buffs ]( player_t* p ) {
    static constexpr std::array<stat_e, 4> ratings = { STAT_VERSATILITY_RATING, STAT_MASTERY_RATING, STAT_HASTE_RATING, STAT_CRIT_RATING };
    buffs.find( util::highest_stat( p, ratings ) ) -> second -> trigger();
    buffs.find( util::lowest_stat( p, ratings ) ) -> second -> trigger();
  } );
}

void temporal_spellthread( special_effect_t& effect )
{
  effect.player->resources.base_multiplier[ RESOURCE_MANA ] *= 1.0 + effect.driver()->effectN( 1 ).percent();
}

}  // namespace enchants

namespace items
{
// Trinkets
  
/* Burgeoning Seed
  * id=193634
  * trigger = 382108
  * consume = 384636
  * buff = 384658 
*/

void consume_pods( special_effect_t& effect )
{
  auto life_pod = find_special_effect( effect.player, 382108 );

  if ( life_pod != nullptr )
  {
    life_pod->custom_buff = make_buff( effect.player, "brimming_life_pod", life_pod->trigger(), effect.item );
    new dbc_proc_callback_t( effect.player, *life_pod );
  }

  struct burgeoning_seed_t : generic_proc_t
  {
    buff_t* pods;
    buff_t* supernatural;

    burgeoning_seed_t( const special_effect_t& effect )
      : generic_proc_t( effect, "burgeoning_seed", effect.trigger() ),
      pods ( buff_t::find( effect.player, "brimming_life_pod" ) ),
      supernatural( buff_t::find( effect.player, "supernatural" ) )
    {
      if ( !supernatural )
        supernatural = create_buff<stat_buff_t>( effect.player, "supernatural", effect.player->find_spell( 384658 ) )
        ->add_stat( STAT_VERSATILITY_RATING, effect.player->find_spell( 384658 )->effectN( 1 ).average( effect.item ) )
        ->add_stat( STAT_MAX_HEALTH, effect.player->find_spell( 384658 )->effectN( 3 ).average( effect.item ) );
    }

    bool ready() override
    {
      return ( pods && pods->check() );
    }

    void execute() override
    {
      supernatural->trigger( pods->stack() );
      pods->expire();
    }
  };

  effect.execute_action = create_proc_action<burgeoning_seed_t>( "burgeoning_seed", effect );
}
  
custom_cb_t idol_of_the_aspects( std::string_view type )
{
  return [ type ]( special_effect_t& effect ) {
    int gems = 0;
    for ( const auto& item : effect.player->items )
      for ( auto gem_id : item.parsed.gem_id )
        if ( gem_id && util::str_in_str_ci( effect.player->dbc->item( gem_id ).name, type ) )
          gems++;

    if ( !gems )
      return;

    auto val = effect.driver()->effectN( 3 ).average( effect.item ) / 4;
    auto gift = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 376643 ) );
    gift->set_stat( STAT_CRIT_RATING, val )
        ->add_stat( STAT_HASTE_RATING, val )
        ->add_stat( STAT_MASTERY_RATING, val )
        ->add_stat( STAT_VERSATILITY_RATING, val );

    auto buff = create_buff<stat_buff_t>( effect.player, effect.trigger() )
      ->set_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect.item ) )
      ->set_max_stack( as<int>( effect.driver()->effectN( 2 ).base_value() ) )
      ->set_expire_at_max_stack( true )
      ->set_stack_change_callback( [ gift ]( buff_t*, int, int new_ ) {
        if ( !new_ )
          gift->trigger();
      } );

    effect.custom_buff = buff;

    new dbc_proc_callback_t( effect.player, effect );

    effect.player->callbacks.register_callback_execute_function(
        effect.driver()->id(), [ buff, gems ]( const dbc_proc_callback_t*, action_t*, action_state_t* ) {
          buff->trigger( gems );
        } );
  };
}

struct DF_darkmoon_deck_t : public darkmoon_spell_deck_t
{
  bool bronzescale;  // shuffles every 2.5s
  bool azurescale;   // shuffles from greatest to least
  bool emberscale;   // no longer shuffles even cards
  bool jetscale;     // no longer shuffles on Ace
  bool sagescale;    // only shuffle on jump NYI

  DF_darkmoon_deck_t( const special_effect_t& e, std::vector<unsigned> c )
    : darkmoon_spell_deck_t( e, std::move( c ) ),
      bronzescale( unique_gear::find_special_effect( player, 382913 ) != nullptr ),
      azurescale( unique_gear::find_special_effect( player, 383336 ) != nullptr ),
      emberscale( unique_gear::find_special_effect( player, 383333 ) != nullptr ),
      jetscale( unique_gear::find_special_effect( player, 383337 ) != nullptr ),
      sagescale( unique_gear::find_special_effect( player, 383339 ) != nullptr )
  {
    if ( bronzescale )
      shuffle_period = player->find_spell( 382913 )->effectN( 1 ).period();
  }

  // TODO: currently in-game the shuffling does not start until the category cooldown 2042 is up. this is currently
  // inconsistently applied between decks, with some having 20s and others have 90s. adjust to the correct behavior when
  // DF goes live.
  void shuffle() override
  {
    // NOTE: assumes last card, and thus highest index, is Ace
    if ( jetscale && top_index == cards.size() - 1 )
      return;

    darkmoon_spell_deck_t::shuffle();
  }

  size_t get_index( bool init ) override
  {
    if ( azurescale )
    {
      if ( init )
      {
        // when you equip it you get the 7 card, but the first shuffle in combat remains at 7. for our purposes since we
        // set the top card on initialization and shuffle again in combat_begin, we initialize to the 8 card, which is
        // second to last.
        top_index = card_ids.size() - 2;
      }
      else
      {
        if ( top_index == 0 )
          top_index = card_ids.size() - 1;
        else
          top_index--;
      }
    }
    else if ( emberscale )
    {
      top_index = player->rng().range( card_ids.size() / 2 ) * 2 + 1;
    }
    else
    {
      darkmoon_spell_deck_t::get_index();
    }

    return top_index;
  }
};

template <typename Base = proc_spell_t>
struct DF_darkmoon_proc_t : public darkmoon_deck_proc_t<Base, DF_darkmoon_deck_t>
{
  using base_t = darkmoon_deck_proc_t<Base, DF_darkmoon_deck_t>;

  DF_darkmoon_proc_t( const special_effect_t& e, std::string_view n, unsigned shuffle_id, std::vector<unsigned> cards )
    : base_t( e, n, shuffle_id, std::move( cards ) )
  {}

  void execute() override
  {
    base_t::execute();

    if ( base_t::deck->shuffle_event )
      base_t::deck->shuffle_event->reschedule( base_t::data().category_cooldown() );
  }
};

void darkmoon_deck_dance( special_effect_t& effect )
{
  struct darkmoon_deck_dance_t : public DF_darkmoon_proc_t<>
  {
    action_t* damage;
    action_t* heal;

    // TODO: confirm mixed order remains true in-game
    // card order is [ 2 3 6 7 4 5 8 A ]
    darkmoon_deck_dance_t( const special_effect_t& e )
      : DF_darkmoon_proc_t( e, "refreshing_dance", 382958,
                            { 382861, 382862, 382865, 382866, 382863, 382864, 382867, 382860 } )
    {
      damage =
        create_proc_action<generic_proc_t>( "refreshing_dance_damage", e, "refreshing_dance_damage", 384613 );
      damage->background = damage->dual = true;
      damage->stats = stats;

      heal =
        create_proc_action<base_generic_proc_t<proc_heal_t>>( "refreshing_dance_heal", e, "refreshing_dance_heal", 384624 );
      heal->name_str_reporting = "Heal";
      heal->background = heal->dual = true;
    }

    void impact( action_state_t* s ) override
    {
      DF_darkmoon_proc_t::impact( s );

      damage->execute_on_target( s->target );
      heal->stats->add_execute( sim->current_time(), s->target );

      timespan_t delay = 0_ms;
      for ( size_t i = 0; i < 5 + deck->top_index; i++ )
      {
        // As this chains to the nearest, assume 5yd melee range
        delay += timespan_t::from_seconds( rng().gauss( 5.0 / travel_speed, sim->travel_variance ) );

        if ( i % 2 )
          make_event( *sim, delay, [ s, this ]() { damage->execute_on_target( s->target ); } );
        else
          make_event( *sim, delay, [ this ]() { heal->execute_on_target( player ); } );
      }
    }
  };
  // TODO: currently this trinket is doing ~1% of the damage it should based on spell data.
  effect.execute_action = create_proc_action<darkmoon_deck_dance_t>( "refreshing_dance", effect );
}

void darkmoon_deck_inferno( special_effect_t& effect )
{
  struct darkmoon_deck_inferno_t : public DF_darkmoon_proc_t<>
  {
    // card order is [ 2 3 4 5 6 7 8 A ]
    darkmoon_deck_inferno_t( const special_effect_t& e )
      : DF_darkmoon_proc_t( e, "darkmoon_deck_inferno", 382958,
                            { 382837, 382838, 382839, 382840, 382841, 382842, 382843, 382835 } )
    {}

    double get_mult( size_t index ) const
    {
      // 7 of fires (index#5) does the base spell_data damage. each card above/below adjusts by 10%
      return 1.0 + ( as<int>( index ) - 5 ) * 0.1;
    }

    double action_multiplier() const override
    {
      return DF_darkmoon_proc_t::action_multiplier() * get_mult( deck->top_index );
    }
  };

  effect.execute_action = create_proc_action<darkmoon_deck_inferno_t>( "darkmoon_deck_inferno", effect );
}

struct awakening_rime_initializer_t : public item_targetdata_initializer_t
{
  awakening_rime_initializer_t() : item_targetdata_initializer_t( 386624 ) {}

  void operator()( actor_target_data_t* td ) const override
  {
    if ( !find_effect( td->source ) )
    {
      td->debuff.awakening_rime = make_buff( *td, "awakening_rime" )->set_quiet( true );
      return;
    }

    assert( !td->debuff.awakening_rime );
    td->debuff.awakening_rime = make_buff( *td, "awakening_rime", td->source->find_spell( 386623 ) );
    td->debuff.awakening_rime->reset();
  }
};

void darkmoon_deck_rime( special_effect_t& effect )
{
  struct darkmoon_deck_rime_t : public DF_darkmoon_proc_t<>
  {
    // card order is [ 2 3 4 5 6 7 8 A ]
    darkmoon_deck_rime_t( const special_effect_t& e )
      : DF_darkmoon_proc_t( e, "awakening_rime", 382958,
                            { 382845, 382846, 382847, 382848, 382849, 382850, 382851, 382844 } )
    {
      // TODO: determine if damage increased per target hit
      auto explode =
          create_proc_action<generic_aoe_proc_t>( "awakened_rime", e, "awakened_rime", e.player->find_spell( 370880 ) );

      e.player->register_on_kill_callback( [ e, explode ]( player_t* t ) {
        if ( e.player->sim->event_mgr.canceled )
          return;

        auto td = e.player->find_target_data( t );
        if ( td && td->debuff.awakening_rime->check() )
          explode->execute_on_target( t );
      } );
    }

    timespan_t get_duration( size_t index ) const
    {
      return 5_s + timespan_t::from_seconds( index );
    }

    void impact( action_state_t* s ) override
    {
      DF_darkmoon_proc_t::impact( s );

      auto td = player->get_target_data( s->target );
      td->debuff.awakening_rime->trigger( get_duration( deck->top_index ) );
    }
  };

  effect.execute_action = create_proc_action<darkmoon_deck_rime_t>( "awakening_rime", effect );
}

void darkmoon_deck_watcher( special_effect_t& effect )
{
  struct darkmoon_deck_watcher_t : public DF_darkmoon_proc_t<proc_heal_t>
  {
    struct watchers_blessing_absorb_buff_t : public absorb_buff_t
    {
      buff_t* stat;

      watchers_blessing_absorb_buff_t( const special_effect_t& e )
        : absorb_buff_t( e.player, "watchers_blessing_absorb", e.driver(), e.item )
      {
        set_name_reporting( "Absorb" );

        stat = create_buff<stat_buff_t>( player, "watchers_blessing_stat", player->find_spell( 384560 ), item );
        stat->set_name_reporting( "Stat" );
      }

      void expire_override( int s, timespan_t d ) override
      {
        absorb_buff_t::expire_override( s, d );

        stat->trigger( d );
      }
    };

    buff_t* shield;

    // TODO: confirm mixed order remains true in-game
    // card order is [ 2 3 6 7 4 5 8 A ]
    darkmoon_deck_watcher_t( const special_effect_t& e )
      : DF_darkmoon_proc_t( e, "watchers_blessing", 382958,
                            { 382853, 382854, 382857, 382858, 382855, 382856, 382859, 382852 } )
    {
      shield = buff_t::find( player, "watchers_blessing_absorb" );
      if ( !shield )
        shield = make_buff<watchers_blessing_absorb_buff_t>( e );
    }

    timespan_t get_duration( size_t index ) const
    {
      return 5_s + timespan_t::from_seconds( index );
    }

    void execute() override
    {
      DF_darkmoon_proc_t::execute();

      auto dur = get_duration( deck->top_index );
      shield->trigger( dur );

      // TODO: placeholder value put at 2s before depletion. change to reasonable value.
      auto deplete = rng().gauss( sim->dragonflight_opts.darkmoon_deck_watcher_deplete, 1_s );
      clamp( deplete, 0_ms, dur );

      make_event( *sim, deplete, [ this ]() { shield->expire(); } );
    }
  };

  effect.execute_action = create_proc_action<darkmoon_deck_watcher_t>( "watchers_blessing", effect );
  effect.buff_disabled = true;
}

void alacritous_alchemist_stone( special_effect_t& effect )
{
  new dbc_proc_callback_t( effect.player, effect );

  auto cd = effect.player->get_cooldown( "potion" );
  auto cd_adj = -timespan_t::from_seconds( effect.driver()->effectN( 1 ).base_value() );

  effect.player->callbacks.register_callback_execute_function(
      effect.spell_id, [ cd, cd_adj ]( const dbc_proc_callback_t* cb, action_t*, action_state_t* ) {
        cb->proc_buff->trigger();
        cd->adjust( cd_adj );
      } );
}

void bottle_of_spiraling_winds( special_effect_t& effect )
{
  // TODO: determine what happens on buff refresh
  auto buff = create_buff<stat_buff_t>( effect.player, effect.trigger() );
  // TODO: confirm it continues to use the driver's coeff and not the actual buff's coeff
  buff->set_stat( STAT_AGILITY, effect.driver()->effectN( 1 ).average( effect.item ) );

  // TODO: this doesn't function in-game atm.
  /*
  auto decrement = create_buff<buff_t>( effect.player, effect.player->find_spell( 383758 ) )
    ->set_quiet( true )
    ->set_tick_callback( [ buff ]( buff_t*, int, timespan_t ) { buff->decrement(); } );

  buff->set_stack_change_callback( [ decrement ]( buff_t* b, int, int new_ ) {
    if ( b->at_max_stacks() )
    {
      b->freeze_stacks = true;
      decrement->trigger();
    }
    else if ( !new_ )
    {
      b->freeze_stacks = false;
    }
  } );
  */
  effect.custom_buff = buff;

  new dbc_proc_callback_t( effect.player, effect );
}

void conjured_chillglobe( special_effect_t& effect )
{
  struct conjured_chillglobe_proxy_t : public action_t
  {
    action_t* damage;
    action_t* mana;
    double mana_level;

    conjured_chillglobe_proxy_t( const special_effect_t& e )
      : action_t( action_e::ACTION_USE, "conjured_chillglobe", e.player, e.driver() )
    {
      dual = true;

      auto value_data = e.player->find_spell( 377450 );
      mana_level = value_data->effectN( 1 ).percent();

      damage = new generic_proc_t( e, "conjured_chillglobe_damage", 377451 );
      damage->base_dd_min = damage->base_dd_max = value_data->effectN( 2 ).average( e.item );
      damage->stats = stats;

      mana = new generic_proc_t( e, "conjured_chillglobe_mana", 381824 );
      mana->energize_amount = value_data->effectN( 3 ).average( e.item );
      mana->harmful = false;
      mana->name_str_reporting = "conjured_chillglobe";
    }

    result_e calculate_result( action_state_t* /* state */ ) const override
    { return RESULT_HIT; }

    void execute() override
    {
      action_t::execute();

      if ( player->resources.active_resource[ RESOURCE_MANA ] && player->resources.pct( RESOURCE_MANA ) < mana_level )
        mana->execute();
      else
        damage->execute_on_target( target );
    }
  };

  effect.execute_action = create_proc_action<conjured_chillglobe_proxy_t>( "conjured_chillglobe", effect );
}

struct skewering_cold_initializer_t : public item_targetdata_initializer_t
{
  skewering_cold_initializer_t() : item_targetdata_initializer_t( 383931 ) {}

  void operator()( actor_target_data_t* td ) const override
  {
    if ( !find_effect( td->source ) )
    {
      td->debuff.skewering_cold = make_buff( *td, "skewering_cold" )->set_quiet( true );
      return;
    }

    assert( !td->debuff.skewering_cold );
    td->debuff.skewering_cold =
        make_buff( *td, "skewering_cold", td->source->find_spell( spell_id )->effectN( 1 ).trigger() );
    td->debuff.skewering_cold->reset();
  }
};

void globe_of_jagged_ice( special_effect_t& effect )
{
  auto impale = find_special_effect( effect.player, 383931 );
  new dbc_proc_callback_t( effect.player, *impale );

  effect.player->callbacks.register_callback_execute_function(
      impale->spell_id, []( const dbc_proc_callback_t* cb, action_t*, action_state_t* s ) {
        cb->listener->get_target_data( s->target )->debuff.skewering_cold->trigger();
      } );

  struct breaking_the_ice_t : public generic_aoe_proc_t
  {
    breaking_the_ice_t( const special_effect_t& e ) : generic_aoe_proc_t( e, "breaking_the_ice", 388948, true )
    {
      // Tooltip and damage value is based on max stacks
      base_dd_min = base_dd_max = data().effectN( 1 ).average( e.item ) / data().max_stacks();
    }

    bool target_ready( player_t* t ) override
    {
      return generic_aoe_proc_t::target_ready( t ) && player->get_target_data( t )->debuff.skewering_cold->check();
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      return generic_aoe_proc_t::composite_target_multiplier( t ) *
             player->get_target_data( t )->debuff.skewering_cold->stack();
    }

    void impact( action_state_t* s ) override
    {
      generic_aoe_proc_t::impact( s );
      player->get_target_data( s->target )->debuff.skewering_cold->expire();
    }
  };

  effect.execute_action = create_proc_action<breaking_the_ice_t>( "breaking_the_ice", *impale );
}

void irideus_fragment( special_effect_t& effect )
{
  auto reduce = new special_effect_t( effect.player );
  reduce->name_str = "crumbling_power_reduce";
  reduce->type = SPECIAL_EFFECT_EQUIP;
  reduce->source = SPECIAL_EFFECT_SOURCE_ITEM;
  reduce->spell_id = effect.spell_id;
  reduce->cooldown_ = effect.driver()->internal_cooldown();
  // TODO: determine what exactly the flags are. At bare minimum reduced per cast. Certain other non-cast procs will
  // also reduce. Maybe require individually tailoring per module a la Bron
  reduce->proc_flags_ = PF_ALL_DAMAGE | PF_ALL_HEAL;
  reduce->proc_flags2_ = PF2_CAST | PF2_CAST_DAMAGE | PF2_CAST_HEAL;
  reduce->set_can_proc_from_procs( true );
  reduce->set_can_only_proc_from_class_abilites( true );
  effect.player->special_effects.push_back( reduce );

  auto cb = new dbc_proc_callback_t( effect.player, *reduce );
  cb->initialize();
  cb->deactivate();


  auto buff = create_buff<stat_buff_t>( effect.player, effect.driver() );
  buff->set_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect.item ) )
      ->set_reverse( true )
      ->set_cooldown( 0_ms )
      ->set_stack_change_callback( [ cb ]( buff_t*, int old_, int new_ ) {
    if ( !old_ )
      cb->activate();
    else if ( !new_ )
      cb->deactivate();
  } );

  effect.custom_buff = buff;

  effect.player->callbacks.register_callback_execute_function(
      effect.spell_id, [ buff ]( const dbc_proc_callback_t*, action_t*, action_state_t* ) { buff->trigger(); } );
}

// TODO: Do properly and add both drivers
// 383798 = Driver for you + value
// 383799 = Stat buff for both
// 383803 = Unused buff
// 386578 = Driver + on-use buff on target
// 389581 = On-use buff on you
// 398396 = On-use
void emerald_coachs_whistle( special_effect_t& effect )
{
  auto buff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 383799 ) )
    ->set_stat_from_effect( 1, effect.driver()->effectN( 2 ).average( effect.item ) );

  effect.custom_buff = buff;

  // self driver procs off druid hostile abilities as well as shadow hostile abilities
  if ( effect.player->type == player_e::DRUID || effect.player->specialization() == PRIEST_SHADOW )
    effect.proc_flags_ |= PF_MAGIC_SPELL | PF_MELEE_ABILITY;

  new dbc_proc_callback_t( effect.player, effect );

  // Pretend we are our bonded partner for the sake of procs from partner
  auto coached = new special_effect_t( effect.player );
  coached->type = SPECIAL_EFFECT_EQUIP;
  coached->source = SPECIAL_EFFECT_SOURCE_ITEM;
  coached->spell_id = 386578;
  coached->custom_buff = buff;
  effect.player->special_effects.push_back( coached );

  new dbc_proc_callback_t( effect.player, *coached );
}

struct spiteful_storm_initializer_t : public item_targetdata_initializer_t
{
  spiteful_storm_initializer_t() : item_targetdata_initializer_t( 377466 ) {}

  void operator()( actor_target_data_t* td ) const override
  {
    if ( !find_effect( td->source ) )
    {
      td->debuff.grudge = make_buff( *td, "grudge" )->set_quiet( true );
      return;
    }

    assert( !td->debuff.grudge );
    td->debuff.grudge = make_buff( *td, "grudge", td->source->find_spell( 382428 ) );
    td->debuff.grudge->reset();
  }
};

void erupting_spear_fragment( special_effect_t& effect )
{
  struct erupting_spear_fragment_t : public generic_aoe_proc_t
  {
    stat_buff_t* buff;
    double return_speed;

    erupting_spear_fragment_t( const special_effect_t& e )
      : generic_aoe_proc_t( e, "erupting_spear_fragment", e.trigger() ),
        return_speed( e.player->find_spell( 381586 )->missile_speed() )
    {
      auto values = player->find_spell( 381484 );

      travel_speed = e.driver()->missile_speed();
      aoe = -1;
      base_dd_min = base_dd_max = values->effectN( 1 ).average( item );

      buff = create_buff<stat_buff_t>( player, "erupting_flames", player->find_spell( 381476 ) );
      buff->set_stat( STAT_CRIT_RATING, values->effectN( 2 ).average( item ) );
    }

    void impact( action_state_t* s ) override
    {
      generic_aoe_proc_t::impact( s );

      make_event( *sim, timespan_t::from_seconds( player->get_player_distance( *s->target ) / return_speed ),
                  [ this ]() { buff->trigger(); } );
    }
  };

  effect.execute_action = create_proc_action<erupting_spear_fragment_t>( "erupting_spear_fragment", effect );
}

void furious_ragefeather( special_effect_t& effect )
{
  struct soulseeker_arrow_repeat_cb_t : public dbc_proc_callback_t
  {
    buff_t* buff;

    soulseeker_arrow_repeat_cb_t( const special_effect_t& e, buff_t* b ) : dbc_proc_callback_t( e.player, e ), buff( b )
    {}

    void execute( action_t* a, action_state_t* s ) override
    {
      buff->expire();

      dbc_proc_callback_t::execute( a, s );
    }
  };

  auto arrow = create_proc_action<generic_proc_t>( "soulseeker_arrow", effect, "soulseeker_arrow", 388755 );
  arrow->base_td = effect.driver()->effectN( 2 ).average( effect.item );

  effect.execute_action = arrow;

  new dbc_proc_callback_t( effect.player, effect );

  unsigned repeat_id = 389407;

  auto buff = create_buff<buff_t>( effect.player, effect.player->find_spell( repeat_id ) );

  auto repeat = new special_effect_t( effect.player );
  repeat->type = SPECIAL_EFFECT_EQUIP;
  repeat->source = SPECIAL_EFFECT_SOURCE_ITEM;
  repeat->spell_id = repeat_id;
  repeat->execute_action = arrow;
  effect.player->special_effects.push_back( repeat );

  auto cb = new soulseeker_arrow_repeat_cb_t( *repeat, buff );
  cb->initialize();
  cb->deactivate();

  buff->set_stack_change_callback( [ cb ]( buff_t*, int, int new_ ) {
    if ( new_ )
      cb->activate();
    else
      cb->deactivate();
  } );

  auto p = effect.player;
  p->register_on_kill_callback( [ p, buff ]( player_t* t ) {
    if ( p->sim->event_mgr.canceled )
      return;

    auto d = t->find_dot( "soulseeker_arrow", p );
    if ( d && d->remains() > 0_ms )
      buff->trigger();
  } );
}

void idol_of_pure_decay( special_effect_t& effect )
{
  struct idol_of_pure_decay_cb_t : public dbc_proc_callback_t
  {
    action_t* damage;
    timespan_t dur;
    int count;

    idol_of_pure_decay_cb_t( const special_effect_t& e )
      : dbc_proc_callback_t( e.player, e ),
        dur( e.trigger()->duration() ),
        count( as<int>( e.driver()->effectN( 1 ).base_value() ) )
    {
      damage = create_proc_action<generic_aoe_proc_t>( "pure_decay", effect, "pure_decay", 388739, true );
      damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 2 ).average( effect.item );
    }

    void execute( action_t*, action_state_t* ) override
    {
      // damage pulses 3 times over 3s.
      // TODO: confirm if trinket can proc again during the 3s.
      make_repeating_event( listener->sim, dur / count, [ this ]() { damage->execute(); }, count );
    }    
  };

  new idol_of_pure_decay_cb_t( effect );
}

void shikaari_huntress_arrowhead( special_effect_t& effect )
{
  // against elite enemies, gives 10% more stats (default)
  // against normal enemies, gives 10% less stats (NYI)
  effect.stat = util::translate_rating_mod( effect.trigger()->effectN( 1 ).misc_value1() );
  effect.stat_amount = effect.trigger()->effectN( 1 ).average( effect.item ) * 1.1;

  new dbc_proc_callback_t( effect.player, effect );
}

void spiteful_storm( special_effect_t& effect )
{
  struct spiteful_stormbolt_t : public generic_proc_t
  {
    buff_t* gathering;
    dbc_proc_callback_t* callback;
    player_t* grudge;

    spiteful_stormbolt_t( const special_effect_t& e, buff_t* b, dbc_proc_callback_t* cb )
      : generic_proc_t( e, "spiteful_stormbolt", e.player->find_spell( 382426 ) ),
        gathering( b ),
        callback( cb ),
        grudge( nullptr )
    {
      base_td = e.driver()->effectN( 1 ).average( e.item );
    }

    void set_target( player_t* t ) override
    {
      if ( grudge )
        target = grudge;
      else
        generic_proc_t::set_target( t );
    }

    double composite_ta_multiplier( const action_state_t* s ) const override
    {
      return generic_proc_t::composite_ta_multiplier( s ) * ( 1.0 + gathering->check_stack_value() );
    }

    void impact( action_state_t* s ) override
    {
      generic_proc_t::impact( s );

      if ( !grudge )
      {
        grudge = s->target;
        assert( grudge );
        player->get_target_data( grudge )->debuff.grudge->trigger();
      }
    }

    void last_tick( dot_t* d ) override
    {
      generic_proc_t::last_tick( d );

      callback->activate();

      make_event( *sim, travel_time(), [ this ]() { gathering->trigger(); } );
    }
  };

  auto spite = create_buff<buff_t>( effect.player, effect.player->find_spell( 382425 ) );
  spite->set_default_value( effect.driver()->effectN( 2 ).base_value() )
       ->set_max_stack( as<int>( spite->default_value ) )
       ->set_expire_at_max_stack( true );

  effect.custom_buff = spite;
  effect.proc_flags2_ = PF2_ALL_CAST;
  auto cb = new dbc_proc_callback_t( effect.player, effect );

  auto gathering = create_buff<buff_t>( effect.player, "gathering_storm_trinket", effect.player->find_spell( 394864 ) );
  gathering->set_default_value( 0.1 );  // increases damage by 10% per stack, value from testing, not found in spell data
  gathering->set_name_reporting( "gathering_storm" );

  auto stormbolt = debug_cast<spiteful_stormbolt_t*>(
      create_proc_action<spiteful_stormbolt_t>( "spiteful_stormbolt", effect, gathering, cb ) );

  spite->set_stack_change_callback( [ stormbolt, cb ]( buff_t* b, int, int new_ ) {
    if ( !new_ )
    {
      cb->deactivate();
      stormbolt->execute_on_target( b->player->target );  // always execute_on_target so set_target is properly called
    }
  } );

  int stack_increase = as<int>( effect.driver()->effectN( 3 ).base_value() );
  gathering->set_stack_change_callback( [ spite, stack_increase ]( buff_t*, int, int new_ ) {
    if ( new_ )
      spite->set_max_stack( as<int>( spite->default_value ) + new_ * stack_increase );
    else
      spite->set_max_stack( as<int>( spite->default_value ) );
  } );

  auto p = effect.player;
  p->register_on_kill_callback( [ p, stormbolt, gathering ]( player_t* t ) {
    if ( p->sim->event_mgr.canceled )
      return;

    if ( stormbolt->grudge && t->actor_spawn_index == stormbolt->grudge->actor_spawn_index )
    {
      stormbolt->grudge = nullptr;
      gathering->expire();
    }
  } );
  effect.player->register_combat_begin( [ cb ]( player_t* ) { cb->activate(); } );
}

void spoils_of_neltharus( special_effect_t& effect )
{
  if ( create_fallback_buffs( effect, { "spoils_of_neltharus_crit", "spoils_of_neltharus_haste",
                                        "spoils_of_neltharus_mastery", "spoils_of_neltharus_vers" } ) )
  {
    return;
  }

  struct spoils_of_neltharus_t : public proc_spell_t
  {
    using buffs = std::vector<std::pair<buff_t*, buff_t*>>;

    struct spoils_of_neltharus_cb_t : public dbc_proc_callback_t
    {
      buffs& buff_list;

      spoils_of_neltharus_cb_t( const special_effect_t& e, buffs* list )
        : dbc_proc_callback_t( e.player, e ), buff_list( *list )
      {}

      void execute( action_t*, action_state_t* ) override
      {
        buff_list.front().first->expire();
        std::rotate( buff_list.begin(), buff_list.begin() + rng().range( 1, 4 ), buff_list.end() );
        buff_list.front().first->trigger();
      }
    };

    buffs buff_list;
    dbc_proc_callback_t* cb;

    spoils_of_neltharus_t( const special_effect_t& e ) : proc_spell_t( e )
    {
      auto shuffle = find_special_effect( e.player, 381766 );
      cb = new spoils_of_neltharus_cb_t( *shuffle, &buff_list );
      cb->initialize();
      cb->activate();

      auto init_buff = [ this, shuffle ]( std::string n, unsigned id ) {
        auto spell = player->find_spell( id );

        auto counter = make_buff<buff_t>( player, "spoils_of_neltharus_" + n, spell )
          ->set_quiet( true );

        auto stat = make_buff<stat_buff_t>( player, "spoils_of_neltharus_stat_" + n, spell )
          ->add_stat_from_effect( 1, shuffle->driver()->effectN( 1 ).average( item ) )
          ->set_name_reporting( util::inverse_tokenize( n ) )
          ->set_duration( timespan_t::from_seconds( data().effectN( 2 ).base_value() ) );

        buff_list.emplace_back( counter, stat );
      };

      init_buff( "crit", 381954 );
      init_buff( "haste", 381955 );
      init_buff( "mastery", 381956 );
      init_buff( "vers", 381957 );

      rng().shuffle( buff_list.begin(), buff_list.end() );
    }

    void reset() override
    {
      proc_spell_t::reset();

      cb->activate();
    }

    void execute() override
    {
      proc_spell_t::execute();

      buff_list.front().first->expire();
      buff_list.front().second->trigger();

      cb->deactivate();
      // TODO: ~90+s before the counter buff can be proc'd again. Find spell data source for this if possible.
      make_event( *sim, 90_s, [ this ]() { cb->activate(); } );
    }
  };

  effect.execute_action = create_proc_action<spoils_of_neltharus_t>( "spoils_of_neltharus", effect );
}

void sustaining_alchemist_stone( special_effect_t& effect )
{
  effect.stat = effect.player->convert_hybrid_stat( STAT_STR_AGI_INT );
  effect.stat_amount = effect.driver()->effectN( 2 ).average( effect.item );

  new dbc_proc_callback_t( effect.player, effect );
}

void timebreaching_talon( special_effect_t& effect )
{
  auto debuff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 384050 ) );
  debuff->set_stat( STAT_INTELLECT, effect.driver()->effectN( 1 ).average( effect.item ) );

  auto buff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 382126 ) );
  buff->set_stat( STAT_INTELLECT, effect.driver()->effectN( 2 ).average( effect.item ) )
      ->set_stack_change_callback( [ debuff ]( buff_t*, int, int new_ ) {
        if ( !new_ )
          debuff->trigger();
      } );

  effect.custom_buff = buff;
}

void voidmenders_shadowgem( special_effect_t& effect )
{
  auto stacking_buff = create_buff<stat_buff_t>( effect.player, "voidmenders_shadowgem_stacks", effect.player->find_spell( 397399 ) );
  stacking_buff->set_cooldown( 0_ms );
  stacking_buff->set_chance( 1.0 );
  stacking_buff->set_max_stack( 20 );
  stacking_buff->set_stat( STAT_CRIT_RATING, effect.driver()->effectN( 2 ).average( effect.item ) );

  auto stacking_driver                = new special_effect_t( effect.player );
  stacking_driver->name_str           = "voidmenders_shadowgem_stacks";
  stacking_driver->type               = SPECIAL_EFFECT_EQUIP;
  stacking_driver->source             = SPECIAL_EFFECT_SOURCE_ITEM;
  stacking_driver->proc_flags_        = effect.driver()->proc_flags();
  stacking_driver->proc_flags2_       = PF2_CAST_HEAL;
  stacking_driver->spell_id           = effect.driver()->id();
  stacking_driver->cooldown_          = 0_ms;
  stacking_driver->cooldown_category_ = 0;
  stacking_driver->custom_buff        = stacking_buff;

  // TODO: Check this. As of 28/12/22 every single spell on shadow procs this item for some reason.
  if ( effect.player->specialization() == PRIEST_SHADOW )
  {
    stacking_driver->proc_flags_ |= PF_ALL_DAMAGE;
    stacking_driver->proc_flags2_ |= PF2_CAST | PF2_CAST_DAMAGE;
  }

  effect.player->special_effects.push_back( stacking_driver );

  auto stacking_cb = new dbc_proc_callback_t( effect.player, *stacking_driver );
  stacking_cb->initialize();
  stacking_cb->deactivate();

  auto buff = create_buff<stat_buff_t>( effect.player, "voidmenders_shadowgem", effect.player->find_spell( 397399 ) );
  buff->set_chance( 1.0 );
  buff->set_stat( STAT_CRIT_RATING, effect.driver()->effectN( 1 ).average( effect.item ) )
      ->set_stack_change_callback( [ stacking_cb, stacking_buff ]( buff_t*, int old_, int new_ ) {
        if ( new_ )
          stacking_cb->activate();
        else
        {
          stacking_cb->deactivate();
          stacking_buff->expire();
        }
      } );

  effect.ppm_ = 0;
  effect.proc_chance_ = 1.0;

  effect.custom_buff = buff;
}

void umbrelskuls_fractured_heart( special_effect_t& effect )
{
  auto dot = create_proc_action<generic_proc_t>( "crystal_sickness", effect, "crystal_sickness", effect.trigger() );
  dot->base_td = effect.driver()->effectN( 2 ).average( effect.item );

  effect.execute_action = dot;

  new dbc_proc_callback_t( effect.player, effect );

  // TODO: explosion doesn't work. placeholder using existing data (which may be wrong)
  // TODO: determine if damage increases per target hit
  auto explode = create_proc_action<generic_aoe_proc_t>( "breaking_the_ice", effect, "breaking_the_ice", 388948 );
  explode->base_dd_min = explode->base_dd_max = effect.driver()->effectN( 3 ).average( effect.item );

  auto p = effect.player;
  p->register_on_kill_callback( [ p, explode ]( player_t* t ) {
    if ( p->sim->event_mgr.canceled )
      return;

    auto d = t->find_dot( "crystal_sickness", p );
    if ( d && d->remains() > 0_ms )
      explode->execute();
  } );
}

void way_of_controlled_currents( special_effect_t& effect )
{
  auto stack =
      create_buff<buff_t>( effect.player, "way_of_controlled_currents_stack", effect.player->find_spell( 381965 ) )
          ->set_name_reporting( "Stack" )
          ->add_invalidate( CACHE_ATTACK_SPEED );
  stack->set_default_value( stack->data().effectN( 1 ).average( effect.item ) * 0.01 );

  effect.player->buffs.way_of_controlled_currents = stack;

  auto surge =
      create_buff<buff_t>( effect.player, "way_of_controlled_currents_surge", effect.player->find_spell( 381966 ) )
          ->set_name_reporting( "Surge" );

  auto lockout =
      create_buff<buff_t>( effect.player, "way_of_controlled_currents_lockout", effect.player->find_spell( 397621 ) )
          ->set_quiet( true );

  auto surge_driver = new special_effect_t( effect.player );
  surge_driver->type = SPECIAL_EFFECT_EQUIP;
  surge_driver->source = SPECIAL_EFFECT_SOURCE_ITEM;
  surge_driver->spell_id = surge->data().id();
  surge_driver->cooldown_ = surge->data().internal_cooldown();
  effect.player->special_effects.push_back( surge_driver );

  auto surge_damage = create_proc_action<generic_proc_t>( "way_of_controlled_currents", *surge_driver,
                                                          "way_of_controlled_currents", 381967 );

  surge_damage->base_dd_min = surge_damage->base_dd_max = effect.driver()->effectN( 5 ).average( effect.item );

  surge_driver->execute_action = surge_damage;

  auto surge_cb = new dbc_proc_callback_t( effect.player, *surge_driver );
  surge_cb->initialize();
  surge_cb->deactivate();

  stack->set_stack_change_callback( [ surge ]( buff_t* b, int, int ) {
    if ( b->at_max_stacks() )
      surge->trigger();
  } );

  surge->set_stack_change_callback( [ stack, lockout, surge_cb ]( buff_t*, int, int new_ ) {
    if ( new_ )
    {
      surge_cb->activate();
    }
    else
    {
      surge_cb->deactivate();
      stack->expire();
      lockout->trigger();
    }
  } );

  effect.custom_buff = stack;
  effect.proc_flags2_ = PF2_CRIT;
  auto stack_cb = new dbc_proc_callback_t( effect.player, effect );

  lockout->set_stack_change_callback( [ stack_cb ]( buff_t*, int, int new_ ) {
    if ( new_ )
      stack_cb->deactivate();
    else
      stack_cb->activate();
  } );
}

void whispering_incarnate_icon( special_effect_t& effect )
{
  bool has_heal = false, has_tank = false, has_dps = false;

  auto splits = util::string_split<std::string_view>(
      effect.player->sim->dragonflight_opts.whispering_incarnate_icon_roles, "/" );
  for ( auto s : splits )
  {
    if ( util::str_compare_ci( s, "heal" ) )
      has_heal = true;
    else if ( util::str_compare_ci( s, "tank" ) )
      has_tank = true;
    else if ( util::str_compare_ci( s, "dps" ) )
      has_dps = true;
    else
      throw std::invalid_argument( "Invalid string for dragonflight.whispering_incarnate_icon_roles." );
  }

  unsigned buff_id = 0, proc_buff_id = 0;

  // single inspiration
  constexpr unsigned dps_buff_id = 382082;   // inspired by frost
  constexpr unsigned tank_buff_id = 382081;  // inspired by earth
  constexpr unsigned heal_buff_id = 382083;  // inspired by flame

  switch ( effect.player->specialization() )
  {
    case WARRIOR_PROTECTION:
    case PALADIN_PROTECTION:
    case DEATH_KNIGHT_BLOOD:
    case MONK_BREWMASTER:
    case DRUID_GUARDIAN:
    case DEMON_HUNTER_VENGEANCE:
      buff_id = 382078;  // mark of earth

      if ( has_dps && has_heal )
        proc_buff_id = 394460;  // inspired by frost and fire
      else if ( has_dps )
        proc_buff_id = dps_buff_id;
      else if ( has_heal )
        proc_buff_id = heal_buff_id;
      else
        proc_buff_id = 0;

      break;
    case PALADIN_HOLY:
    case PRIEST_DISCIPLINE:
    case PRIEST_HOLY:
    case SHAMAN_RESTORATION:
    case MONK_MISTWEAVER:
    case DRUID_RESTORATION:
    case EVOKER_PRESERVATION:
      buff_id = 382080;  // mark of fire

      if ( has_dps && has_tank )
        proc_buff_id = 394462;  // inspired by frost and earth
      else if ( has_dps )
        proc_buff_id = dps_buff_id;
      else if ( has_tank )
        proc_buff_id = tank_buff_id;
      else
        proc_buff_id = 0;

      break;
    default:
      buff_id = 382079;  // mark of frost

      if ( has_heal && has_tank )
        proc_buff_id = 394461;  // inspired by fire and earth
      else if ( has_heal )
        proc_buff_id = heal_buff_id;
      else if ( has_tank )
        proc_buff_id = tank_buff_id;

      break;
  }

  auto buff_data = effect.player->find_spell( buff_id );
  auto buff = create_buff<stat_buff_t>( effect.player, buff_data );
  buff->set_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect.item ) )
      ->set_constant_behavior( buff_constant_behavior::ALWAYS_CONSTANT )
      ->set_rppm( rppm_scale_e::RPPM_DISABLE );

  effect.player->register_combat_begin( [ buff ]( player_t* ) {
    buff->trigger();
  } );

  if ( proc_buff_id )
  {
    auto proc_buff_data = effect.player->find_spell( proc_buff_id );
    auto proc_buff      = create_buff<stat_buff_t>( effect.player, proc_buff_data );
    double amount       = effect.driver()->effectN( 2 ).average( effect.item );

    for ( auto& s : proc_buff->stats )
      s.amount = amount;

    effect.spell_id = buff_id;
    effect.custom_buff = proc_buff;

    new dbc_proc_callback_t( effect.player, effect );
  }
}

void the_cartographers_calipers( special_effect_t& effect )
{
  effect.trigger_spell_id = 384114;
  effect.discharge_amount = effect.driver()->effectN( 1 ).average( effect.item );

  new dbc_proc_callback_t( effect.player, effect );
}

// Rumbling Ruby
// 377454 = Driver
// 382094 = Stacking Buff, triggers 382095 at max stacks
// 382095 = Player Buff triggerd from 382094, triggers 382096
// 382096 = Damage Area trigger
// 382097 = Damage spell
void rumbling_ruby( special_effect_t& effect )
{
  special_effect_t* rumbling_ruby_damage_proc = new special_effect_t( effect.player );
  rumbling_ruby_damage_proc->item = effect.item;

  auto ruby_buff = buff_t::find( effect.player, "rumbling_ruby");
  if ( !ruby_buff )
  {
    auto ruby_proc_spell = effect.player -> find_spell( 382095 );
    ruby_buff = make_buff( effect.player, "rumbling_ruby", ruby_proc_spell );
    rumbling_ruby_damage_proc->spell_id = 382095;

    effect.player -> special_effects.push_back( rumbling_ruby_damage_proc );
  }

  auto power_buff = buff_t::find( effect.player, "rumbling_power" );
  if ( !power_buff )
  {
    auto power_proc_spell = effect.player->find_spell( 382094 );
    power_buff = make_buff<stat_buff_t>(effect.player, "concussive_force", power_proc_spell)
                   ->add_stat( STAT_ATTACK_POWER, effect.driver() -> effectN( 1 ).average( effect.item ));
  }

  struct rumbling_ruby_damage_t : public proc_spell_t
  {
    buff_t* ruby_buff;

    rumbling_ruby_damage_t( const special_effect_t& e, buff_t* b ) :
      proc_spell_t( "rumbling_ruby_damage", e.player, e.player->find_spell( 382097 ), e.item ), ruby_buff( b )
    {
      base_dd_min = base_dd_max = e.player -> find_spell( 377454 ) -> effectN( 2 ).average( e.item );
      background = true;
      aoe = -1;
    }

    double composite_da_multiplier( const action_state_t* s ) const override
    {
       double d = proc_spell_t::composite_da_multiplier( s );

       // 10-28-22 In game testing suggests the damage scaling is 
       // 50% increase above 90% enemy hp
       // 25% above 75% enemy hp
       // 10% above 50% enemy hp

       if (s->target->health_percentage() >= 50 && s->target->health_percentage() <= 75)
       {
         d *= 1.1;
       }
       if (s->target->health_percentage() >= 75 && s->target->health_percentage() <= 90)
       {
         d *= 1.25;
       }
       if (s->target->health_percentage() >= 90)
       {
         d *= 1.5;
       }
      return d;
    }

    void execute() override
    {
      proc_spell_t::execute();
      ruby_buff -> decrement();
    }
  };

  auto proc_object = new dbc_proc_callback_t( effect.player, *rumbling_ruby_damage_proc );
  rumbling_ruby_damage_proc->execute_action = create_proc_action<rumbling_ruby_damage_t>( "rumbling_ruby_damage", *rumbling_ruby_damage_proc, ruby_buff );
  ruby_buff -> set_stack_change_callback( [ proc_object, power_buff ]( buff_t* b, int, int new_ )
  {
    if ( new_ == 0 )
    { 
      b->expire();
      power_buff->expire();
      proc_object->deactivate();
    }
    else if ( b->at_max_stacks() )
    {
      proc_object->activate();
    }
  } );

  power_buff -> set_stack_change_callback( [ ruby_buff ]( buff_t* b, int, int ) 
  {
    if ( b->at_max_stacks() )
    {
      ruby_buff->trigger();
    }
  } );

  effect.custom_buff = power_buff;
  new dbc_proc_callback_t( effect.player, effect );
  proc_object->deactivate();
}

// Storm-Eater's Boon
// 377453 Driver
// 382090 Damage Effect & Stacking Buff
// 382092 Damage Value
void storm_eaters_boon( special_effect_t& effect )
{
  buff_t* stack_buff;
  buff_t* main_buff;

  main_buff = make_buff( effect.player, "stormeaters_boon", effect.player->find_spell(377453))
      ->set_cooldown( 0_ms );
  stack_buff = make_buff( effect.player, "stormeaters_boon_stacks", effect.player->find_spell( 382090 ))
      ->set_duration( effect.player -> find_spell( 377453 )->duration() )
      ->set_cooldown( 0_ms );

  effect.custom_buff = main_buff;

  struct storm_eaters_boon_damage_t : public proc_spell_t
  {
    buff_t* stack_buff;
    storm_eaters_boon_damage_t( const special_effect_t& e, buff_t* b ) :
      proc_spell_t( "stormeaters_boon_damage", e.player, e.player->find_spell( 382090 ), e.item ), stack_buff( b )
    {
      background = true;
      base_dd_min = base_dd_max = e.player->find_spell( 382092 )->effectN( 1 ).average(e.item);
      name_str_reporting = "stormeaters_boon";
    }

     double composite_da_multiplier( const action_state_t* s ) const override
    {
      double m = proc_spell_t::composite_da_multiplier( s );

      m *= 1.0 + ( stack_buff -> stack() * 0.1 );

      return m;
    }

     void execute() override
     {
       proc_spell_t::execute();
       stack_buff -> trigger();
     }
  };
  action_t* boon_action = create_proc_action<storm_eaters_boon_damage_t>( "stormeaters_boon_damage", effect, stack_buff );
  main_buff->set_refresh_behavior( buff_refresh_behavior::DISABLED );
  main_buff->set_tick_callback( [ boon_action ]( buff_t* /* buff */, int /* current_tick */, timespan_t /* tick_time */ ) 
  {
    boon_action->execute();
  } );
  main_buff->set_stack_change_callback( [ stack_buff ](buff_t*, int, int new_) 
  {
    if( new_ == 0 )
    {
      stack_buff->expire();
    }
  } );
}

// Decoration of Flame
// 377449 Driver
// 382058 Shield Buff
// 394393 Damage and Shield Value
// 397331 Fireball 1
// 397347 - 397353 Fireballs 2-8
void decoration_of_flame( special_effect_t& effect )
{
  buff_t* buff;

  buff = make_buff( effect.player, "decoration_of_flame", effect.player->find_spell( 377449 ) );

  effect.custom_buff = buff;

  struct decoration_of_flame_damage_t : public proc_spell_t
  {
     buff_t* shield;
     double value;
     unsigned cap;

     decoration_of_flame_damage_t( const special_effect_t& e )
       : proc_spell_t( "decoration_of_flame", e.player, e.player->find_spell( 377449 ), e.item ),
         shield( nullptr ),
         value( e.player->find_spell( 394393 )->effectN( 2 ).average( e.item ) ),
         cap( as<int>( e.driver()->effectN( 3 ).base_value() ) )
     {
       background = true;
       split_aoe_damage = true;
       base_dd_min = base_dd_max = e.player->find_spell( 394393 )->effectN( 1 ).average( e.item );
       aoe = -1;
       radius = 10;
       shield = make_buff<absorb_buff_t>( e.player, "decoration_of_flame_shield", e.player->find_spell( 382058 ) );
     }

    std::vector<player_t*>& target_list() const override
    {
      target_cache.is_valid = false;

      auto& tl = proc_spell_t::target_list();

      tl.erase( std::remove_if( tl.begin(), tl.end(), [ this ]( player_t* ) {
        return rng().roll( sim->dragonflight_opts.decoration_of_flame_miss_chance );
      } ), tl.end() );

      return tl;
    }

     double composite_da_multiplier( const action_state_t* s ) const override
     {
       double m = proc_spell_t::composite_da_multiplier( s );

       // Damage increases by 10% per target based on in game testing
       m *= 1.0 + ( std::min( cap, s->n_targets ) - 1 ) * 0.1;

       return m;
     }

     void execute() override
     {
       proc_spell_t::execute();

       shield->trigger( -1, value * ( 1.0 + ( num_targets_hit - 1 ) * 0.05 ) );
     }
  };

  action_t* action = create_proc_action<decoration_of_flame_damage_t>( "decoration_of_flame", effect );
  buff->set_stack_change_callback( [ action ]( buff_t*, int, int ) {
    action->execute();
  } );
}

// Manic Grieftorch
// 377463 Driver
// 382135 Damage
// 382136 Damage Driver
// 394954 Damage Value
// 382256 AoE Radius
// 382257 ???
// 395703 ???
// 396434 ??? 
void manic_grieftorch( special_effect_t& effect )
{
    struct manic_grieftorch_damage_t : public proc_spell_t
  {
    manic_grieftorch_damage_t( const special_effect_t& e ) :
      proc_spell_t( "manic_grieftorch", e.player, e.player->find_spell( 382135 ), e.item )
    {
      background = true;
      base_dd_min = base_dd_max = e.player->find_spell( 394954 )->effectN( 1 ).average( e.item );
    }
  };
  
  struct manic_grieftorch_missile_t : public proc_spell_t
  {
    manic_grieftorch_missile_t(const special_effect_t& e) :
        proc_spell_t("manic_grieftorch_missile", e.player, e.player->find_spell(382136), e.item)
    {
      background = true;
      aoe = -1;
      radius = e.player -> find_spell( 382256 ) -> effectN( 1 ).radius();
      impact_action = create_proc_action<manic_grieftorch_damage_t>( "manic_grieftorch", e );
    }

    size_t available_targets( std::vector< player_t* >& tl ) const override
    {
    proc_spell_t::available_targets( tl );
    tl.erase( std::remove_if( tl.begin(), tl.end(), [ this ]( player_t* t) {
         if( t == target )
        {
          return false;
        }
        else
        {
          // Has very strange scaling behavior, where it scales with targets very slowly. Using this formula to reduce the cleave chance as target count increases
             return !rng().roll(0.2 * (sqrt(num_targets()) / num_targets()) );
        }
      }), tl.end() );

      return tl.size();
    }
  };

  struct manic_grieftorch_channel_t : public proc_spell_t
  {
    manic_grieftorch_channel_t( const special_effect_t& e ) :
      proc_spell_t( "manic_grieftorch_channel", e.player, e.player->find_spell( 377463 ), e.item)
    {
      background = true;
      channeled = tick_zero = true;
      hasted_ticks = false;
      target_cache.is_valid = false;
      base_tick_time = e.player -> find_spell( 377463 ) -> effectN( 1 ).period();
      tick_action = create_proc_action<manic_grieftorch_missile_t>( "manic_grieftorch_missile", e );
    }

    void last_tick( dot_t* d ) override
    {
      bool was_channeling = player->channeling == this;

      proc_spell_t::last_tick( d );

      if ( was_channeling && !player->readying )
        player->schedule_ready( rng().gauss( sim->channel_lag, sim->channel_lag_stddev ) );
    }
  };

  effect.execute_action = create_proc_action<manic_grieftorch_channel_t>( "manic_grieftorch_channel", effect );
}

// All-Totem of the Master
// TODO - Setup to only proc for tank specs
// 377457 Driver and values
// Effect 1 - Fire/Ice direct damage
// Effect 2 - Ice buff value ?
// Effect 3 - Earth/Air direct damage
// Effect 4 - Fire DoT damage
// Effect 5 - Haste value
// 377458 Earth buff
// 377459 Fire buff
// 377461 Air buff
// 382133 Ice buff
void alltotem_of_the_master( special_effect_t& effect )
{
  effect.type = SPECIAL_EFFECT_NONE;

  struct alltotem_earth_damage_t : public proc_spell_t
  {
    alltotem_earth_damage_t( const special_effect_t& e ) :
      proc_spell_t( "elemental_stance_earth", e.player, e.player->find_spell( 377458 ), e.item )
    {
      background = true;
      aoe = -1;
      base_dd_min = base_dd_max = e.driver()->effectN(3).average(e.item);
    }
  };

  struct alltotem_fire_damage_t : public proc_spell_t
  {
    alltotem_fire_damage_t( const special_effect_t& e ) :
      proc_spell_t( "elemental_stance_fire", e.player, e.player->find_spell( 377459 ), e.item )
    {
      background = true;
      aoe = -1;
      base_dd_min = base_dd_max = e.driver()->effectN(1).average(e.item);
    }
  };

  struct alltotem_fire_dot_damage_t : public proc_spell_t
  {
    alltotem_fire_dot_damage_t( const special_effect_t& e ) :
      proc_spell_t( "elemental_stance_fire_dot", e.player, e.player->find_spell( 377459 ), e.item )
    {
      background = true;
      aoe = -1;
      base_dd_min = base_dd_max = e.driver()->effectN(4).average(e.item);
    }
  };

  struct alltotem_air_damage_t : public proc_spell_t
  {
    alltotem_air_damage_t( const special_effect_t& e ) :
      proc_spell_t( "elemental_stance_air", e.player, e.player->find_spell( 377461 ), e.item )
    {
      background = true;
      aoe = -1;
      base_dd_min = base_dd_max = e.driver()->effectN( 3 ).average( e.item );
    }
  };

  struct alltotem_ice_damage_t : public proc_spell_t
  {
    alltotem_ice_damage_t( const special_effect_t& e ) :
      proc_spell_t( "elemental_stance_ice", e.player, e.player->find_spell( 382133 ), e.item )
    {
      background = true;
      aoe = -1;
      base_dd_min = base_dd_max = e.driver()->effectN( 1 ).average( e.item );
    }
  };

  struct alltotem_buffs_t : public proc_spell_t
  {
    buff_t* earth_buff;
    buff_t* fire_buff;
    buff_t* air_buff;
    buff_t* ice_buff;
    std::vector<buff_t*> buffs;
    buff_t* first;

    alltotem_buffs_t( const special_effect_t& e ) : 
      proc_spell_t( "alltotem_of_the_master", e.player, e.player -> find_spell( 377457 ), e.item )
    {

      earth_buff = make_buff<stat_buff_t>(e.player, "elemental_stance_earth", e.player->find_spell(377458))
          ->add_stat(STAT_BONUS_ARMOR, e.driver()->effectN(5).average(e.item));
      auto earth_damage = create_proc_action<alltotem_earth_damage_t>( "elemental_stance_earth", e );
      earth_buff->set_stack_change_callback( [ earth_damage ](buff_t*, int, int new_) 
      {
        if( new_ == 1 )
        {
          earth_damage->execute();
        }
      } );
      fire_buff = make_buff(e.player, "elemental_stance_fire", e.player->find_spell(377459))
          ->set_period(e.player->find_spell( 377459 )->effectN(3).period());
      auto fire_damage = create_proc_action<alltotem_fire_damage_t>( "elemental_stance_fire", e );
      auto fire_dot = create_proc_action<alltotem_fire_dot_damage_t>( "elemental_stance_fire_dot", e );
      fire_buff->set_stack_change_callback( [ fire_damage ](buff_t*, int, int new_) 
      {
        if( new_ == 1 )
        {
          fire_damage->execute();
        }
      } );
      fire_buff->set_tick_callback( [ fire_dot ]( buff_t* /* buff */, int /* current_tick */, timespan_t /* tick_time */ ) 
      {
        fire_dot->execute();
      } );
      air_buff = make_buff<stat_buff_t>(e.player, "elemental_stance_air", e.player->find_spell(377461))
           ->add_stat(STAT_HASTE_RATING, e.driver()->effectN(5).average(e.item));
      auto air_damage = create_proc_action<alltotem_air_damage_t>( "elemental_stance_air", e );
      air_buff->set_stack_change_callback( [ air_damage ](buff_t*, int, int new_) 
      {
        if( new_ == 1 )
        {
          air_damage->execute();
        }
      } );
      ice_buff = make_buff(e.player, "elemental_stance_ice", e.player->find_spell(382133));
      ice_buff->set_default_value(e.driver()->effectN(2).average(e.item));
      auto ice_damage = create_proc_action<alltotem_ice_damage_t>( "elemental_stance_ice", e );
      ice_buff->set_stack_change_callback( [ ice_damage ](buff_t*, int, int new_) 
      {
        if( new_ == 1 )
        {
          ice_damage->execute();
        }
      } );

      first = buffs.emplace_back( earth_buff );
      buffs.push_back( fire_buff );
      buffs.push_back( air_buff);
      buffs.push_back( ice_buff );

      add_child(earth_damage);
      add_child(fire_damage);
      add_child(fire_dot);
      add_child(ice_damage);
      add_child(air_damage);
    }

    void rotate()
    {
       buffs.front()->trigger();
       std::rotate(buffs.begin(), buffs.begin() + 1, buffs.end());
    }

    void execute() override
    {
      proc_spell_t::execute();
      rotate();
    }

    void reset() override
    {
      proc_spell_t::reset();
      std::rotate( buffs.begin(), range::find( buffs, first ), buffs.end() );
    }
  };

  action_t* action = create_proc_action<alltotem_buffs_t>( "alltotem_of_the_master", effect );

  if ( effect.player->role == ROLE_TANK )
  {
    effect.player->register_combat_begin( [ &effect, action ]( player_t* ) {
    timespan_t base_period = effect.driver()->internal_cooldown();
    timespan_t period =
        base_period + ( effect.player->sim->dragonflight_opts.alltotem_of_the_master_period +
                        effect.player->rng().range( 0_s, 12_s ) / ( 1 + effect.player->sim->target_non_sleeping_list.size() ) );
    make_repeating_event( effect.player->sim, period, [ action ]() { action->execute(); } );
    } );
  }
}

void tome_of_unstable_power( special_effect_t& effect )
{
  auto buff_spell = effect.player->find_spell( 388583 );
  auto data_spell = effect.player->find_spell( 391290 );

  auto buff = create_buff<stat_buff_t>( effect.player, buff_spell );

  buff->set_duration( effect.driver()->duration() );
  buff->set_stat_from_effect( 1, data_spell->effectN( 1 ).average( effect.item ) )
      ->add_stat_from_effect( 2, data_spell->effectN( 2 ).average( effect.item ) );

  effect.custom_buff = buff;
}

// Algethar Puzzle Box
// 383781 Driver and Buff
void algethar_puzzle_box( special_effect_t& effect )
{
  struct solved_the_puzzle_t : public proc_spell_t
  {
    buff_t* buff;
    action_t* use_action;  // if this exists, then we're prechanneling via the APL

    solved_the_puzzle_t( const special_effect_t& e, buff_t* b )
      : proc_spell_t( "solved_the_puzzle", e.player, e.driver(), e.item ), buff( b ), use_action( nullptr )
    {
      background   = true;
      effect       = &e;
      harmful = false;

      for ( auto a : player->action_list )
      {
        if ( a->action_list && a->action_list->name_str == "precombat" && a->name_str == "use_item_" + item->name_str )
        {
          a->harmful = harmful;  // pass down harmful to allow action_t::init() precombat check bypass
          use_action = a;
          use_action->base_execute_time = 2_s;
          break;
        }
      }
    }

    void precombat_buff()
    {
      timespan_t time = 0_ms;

      if ( time == 0_ms )  // No global override, check for an override from an APL variable
      {
        for ( auto v : player->variables )
        {
          if ( v->name_ == "algethar_puzzle_box_precombat_cast" )
          {
            time = timespan_t::from_seconds( v->value() );
            break;
          }
        }
      }

      // shared cd (other trinkets & on-use items)
      auto cdgrp = player->get_cooldown( effect->cooldown_group_name() );

      if ( time == 0_ms )  // No hardcoded override, so dynamically calculate timing via the precombat APL
      {
        time            = 2_s;  // base 2s cast
        const auto& apl = player->precombat_action_list;

        auto it = range::find( apl, use_action );
        if ( it == apl.end() )
        {
          sim->print_debug(
              "WARNING: Precombat /use_item for Algethar Puzzle Box exists but not found in precombat APL!" );
          return;
        }

        cdgrp->start( 1_ms );  // tap the shared group cd so we can get accurate action_ready() checks

        // add cast time or gcd for any following precombat action
        std::for_each( it + 1, apl.end(), [ &time, this ]( action_t* a ) {
          if ( a->action_ready() )
          {
            timespan_t delta =
                std::max( std::max( a->base_execute_time, a->trigger_gcd ) * a->composite_haste(), a->min_gcd );
            sim->print_debug( "PRECOMBAT: Algethar Puzzle Box precast timing pushed by {} for {}", delta,
                              a->name() );
            time += delta;

            return a->harmful;  // stop processing after first valid harmful spell
          }
          return false;
        } );
      }
      else if ( time < 2_s )  // If APL variable can't set to less than cast time
      {
        time = 2_s;
      }

      // how long you cast for
      auto cast = 2_s;
      // total duration of the buff
      auto total = buff->buff_duration();
      // actual duration of the buff you'll get in combat
      auto actual = total + cast - time;
      // cooldown on effect/trinket at start of combat
      auto cd_dur = cooldown->duration + cast - time;
      // shared cooldown at start of combat
      auto cdgrp_dur = std::max( 0_ms, effect->cooldown_group_duration() + cast - time );

      sim->print_debug(
          "PRECOMBAT: Algethar Puzzle Box started {}s before combat via {}, {}s in-combat buff",
          time, use_action ? "APL" : "DRAGONFLIGHT_OPT", actual );

      buff->trigger( 1, buff_t::DEFAULT_VALUE(), 1.0, actual );

      if ( use_action )  // from the apl, so cooldowns will be started by use_item_t. adjust. we are still in precombat.
      {
        make_event( *sim, [ this, cast, time, cdgrp ] {  // make an event so we adjust after cooldowns are started
          cooldown->adjust( cast - time );

          if ( use_action )
            use_action->cooldown->adjust( cast - time );

          cdgrp->adjust( cast - time );
        } );
      }
      else  // via bfa. option override, start cooldowns. we are in-combat.
      {
        cooldown->start( cd_dur );

        if ( use_action )
          use_action->cooldown->start( cd_dur );

        if ( cdgrp_dur > 0_ms )
          cdgrp->start( cdgrp_dur );
      }
    }

    void execute() override
    {
      proc_spell_t::execute();

      if ( !player->in_combat )  // if precombat...
      {
        if ( use_action )  // ...and use_item exists in the precombat apl
        {
          precombat_buff();
        }
      }
    }
  };

  auto buff_spell = effect.player->find_spell( 383781 );
  buff_t* buff    = create_buff<stat_buff_t>( effect.player, buff_spell )
    ->set_stat_from_effect( 1, effect.driver()->effectN( 1 ).average( effect.item ) );
  buff->set_default_value( effect.driver()->effectN( 1 ).average( effect.item ) );

  auto action           = new solved_the_puzzle_t( effect, buff );
  action->use_off_gcd   = true;
  effect.execute_action = action;
}

// Frenzying Signoll Flare
// 382119 Driver
// 384290 Smorf's Ambush
// 384294 Siki's Ambush
// 386168 Barf's Ambush
void frenzying_signoll_flare(special_effect_t& effect)
{

  if ( unique_gear::create_fallback_buffs( effect, { "sikis_ambush_crit_rating", "sikis_ambush_mastery_rating", "sikis_ambush_haste_rating",
                  "sikis_ambush_versatility_rating" } ) )
   return;

  struct smorfs_ambush_t : public proc_spell_t
  {
    smorfs_ambush_t( const special_effect_t& e ) :
    proc_spell_t( "smorfs_ambush", e.player, e.player -> find_spell(384290), e.item)
    {
      background = true;
      base_td = e.driver()->effectN( 3 ).average( e.item );
      base_tick_time = e.player -> find_spell(384290) -> effectN( 1 ).period();
    }
  };

  struct barfs_ambush_t : public proc_spell_t
  {
    barfs_ambush_t( const special_effect_t& e ) :
    proc_spell_t( "barfs_ambush", e.player, e.player -> find_spell(386168), e.item)
    {
      background = true;
      base_dd_min = base_dd_max = e.driver() -> effectN( 2 ).average( e.item );
    }
  };

  struct frenzying_signoll_flare_t : public proc_spell_t
  {
    action_t* smorfs;
    action_t* barfs;
    std::shared_ptr<std::map<stat_e, buff_t*>> siki_buffs;
    // When selecting the highest stat, the priority of equal secondary stats is Vers > Mastery > Haste > Crit.
    std::array<stat_e, 4> ratings;

    frenzying_signoll_flare_t(const special_effect_t& e) :
        proc_spell_t("frenzying_signoll_flare", e.player, e.player -> find_spell(382119), e.item)
    {
      background = true;

      smorfs = create_proc_action<smorfs_ambush_t>( "smorfs_ambush", e );
      barfs = create_proc_action<barfs_ambush_t>( "barfs_ambush", e );

      // Use a separate buff for each rating type so that individual uptimes are reported nicely and APLs can easily
      // reference them. Store these in pointers to reduce the size of the events that use them.
      siki_buffs = std::make_shared<std::map<stat_e, buff_t*>>();
      double amount = e.driver()->effectN( 1 ).average( e.item );
      
      ratings = { STAT_VERSATILITY_RATING, STAT_MASTERY_RATING, STAT_HASTE_RATING,
                                                     STAT_CRIT_RATING };
      for ( auto stat : ratings )
      {
        auto name = std::string( "sikis_ambush_" ) + util::stat_type_string( stat );
        buff_t* buff = buff_t::find( e.player, name );

        if ( !buff )
        {
           buff = make_buff<stat_buff_t>( e.player, name, e.player->find_spell( 384294 ), e.item )
                 ->add_stat( stat, amount );
         }
        ( *siki_buffs )[ stat ] = buff;
      }

      add_child(smorfs);
      add_child(barfs);
    }

    void execute() override
    {
      proc_spell_t::execute();

      auto selected_effect = player->sim->rng().range( 3 );
      
      if( selected_effect == 0 )
      {
        smorfs->execute();
      }

      else if( selected_effect == 1 )
      {
        barfs->execute();
      }

      else if (selected_effect == 2 )
      {
        stat_e max_stat = util::highest_stat( player, ratings );
        ( *siki_buffs )[ max_stat ]->trigger();
      }
    }
  };
  effect.type = SPECIAL_EFFECT_EQUIP;
  effect.execute_action = create_proc_action<frenzying_signoll_flare_t>( "frenzying_signoll_flare", effect );
  new dbc_proc_callback_t( effect.player, effect );
}

// Idol of Trampling Hooves
// 386175 Driver
// 392908 Buff
// 385533 Damage
void idol_of_trampling_hooves(special_effect_t& effect)
{
  auto buff_spell = effect.player->find_spell( 392908 );
  auto buff = create_buff<stat_buff_t>( effect.player , buff_spell );
  buff -> set_default_value(effect.driver()->effectN(1).average(effect.item));
  auto damage = create_proc_action<generic_aoe_proc_t>( "trampling_hooves", effect, "trampling_hooves", 385533 );
  damage->split_aoe_damage = true;
  damage->aoe = -1;
  effect.execute_action = damage;
  effect.custom_buff = buff;
  new dbc_proc_callback_t( effect.player, effect );
}

// Dragon Games Equipment
// 386692 Driver & Buff
// 386708 Damage Driver
// 386709 Damage
// 383950 Damage Value
void dragon_games_equipment(special_effect_t& effect)
{
  auto damage = create_proc_action<generic_proc_t>( "dragon_games_equipment", effect, "dragon_games_equipment", 386708 );
  damage->base_dd_min = damage -> base_dd_max = effect.player->find_spell(383950)->effectN(1).average(effect.item);
  damage->aoe = 0;

  auto buff_spell = effect.player->find_spell( 386692 );
  auto buff = create_buff<buff_t>( effect.player , buff_spell );
  buff->tick_on_application = false;
  // Override Duration to trigger the correct number of missiles. Testing as of 11-14-2022 shows it only spawning 3, rather than the 4 expected by spell data.
  buff->set_duration( ( buff_spell->duration() / 4 ) * effect.player->sim->dragonflight_opts.dragon_games_kicks * effect.player->rng().range( effect.player->sim->dragonflight_opts.dragon_games_rng, 1.25) );
  buff->set_tick_callback( [ damage ]( buff_t* b, int, timespan_t ) {
        damage->execute_on_target( b->player->target );
      } );

  effect.custom_buff = buff;
}

// Bonemaw's Big Toe
// 397400 Driver & Buff
// 397401 Damage
void bonemaws_big_toe(special_effect_t& effect)
{
  auto buff_spell = effect.player->find_spell( 397400 );
  auto buff = create_buff<stat_buff_t>( effect.player , buff_spell );
  auto value = effect.driver()->effectN(1).average(effect.item);
  buff->add_stat( STAT_CRIT_RATING, value );
  auto damage = create_proc_action<generic_aoe_proc_t>( "fetid_breath", effect, "fetid_breath", 397401 );
  buff->set_tick_callback( [ damage ]( buff_t*, int, timespan_t )
    {
      damage->execute();
    } );
  damage->split_aoe_damage = true;
  damage->aoe = -1;
  effect.custom_buff = buff;
}

// Mutated Magmammoth Scale
// 381705 Driver
// 381727 Buff & Damage Driver
// 381760 Damage
void mutated_magmammoth_scale(special_effect_t& effect)
{
  auto buff_spell = effect.player->find_spell( 381727 );
  auto buff = create_buff<buff_t>( effect.player , buff_spell );
  auto damage = create_proc_action<generic_aoe_proc_t>( "mutated_tentacle_slam", effect, "mutated_tentacle_slam", 381760, true );
  buff->set_tick_callback( [ damage ]( buff_t*, int, timespan_t )
    {
      damage->execute();
    } );
  damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN(1).average(effect.item);
  effect.custom_buff = buff;
  new dbc_proc_callback_t( effect.player, effect );
}

// Homeland Raid Horn
// 382139 Driver & Buff
// 384003 Damage Driver
// 384004 Damage
// 387777 Damage value
void homeland_raid_horn(special_effect_t& effect)
{
  auto damage = create_proc_action<generic_aoe_proc_t>( "dwarven_barrage", effect, "dwarven_barrage", 384004, true );
  damage->base_dd_min = damage -> base_dd_max = effect.player->find_spell( 387777 )->effectN( 1 ).average( effect.item );

  auto buff_spell = effect.player->find_spell( 382139 );
  auto buff = create_buff<buff_t>( effect.player , buff_spell );
  buff->set_tick_callback( [ damage ]( buff_t*, int, timespan_t ) {
    damage->execute();
  } );

  effect.custom_buff = buff;
}

// Blazebinder's Hoof
// 383926 Driver & Buff
// 389710 Damage
void blazebinders_hoof(special_effect_t& effect)
{
  struct burnout_wave_t : public proc_spell_t
  {
    double buff_stacks;

    burnout_wave_t( const special_effect_t& e ) :
    proc_spell_t( "burnout_wave", e.player, e.player -> find_spell( 389710 ), e.item), buff_stacks()
    {
      // Value stored in effect 2 appears to be the maximum damage that can be done with 6 stacks. Divide it by max stacks to get damage per stack.
      base_dd_min = base_dd_max = e.driver()->effectN(2).average(e.item) / e.driver()->max_stacks();
      split_aoe_damage = true;
    }

    double composite_da_multiplier( const action_state_t* s ) const override
    {
      double m = proc_spell_t::composite_da_multiplier( s );
      // Damage increased linerally by number of stacks
      m *= buff_stacks;

      return m;
    }
  };

  struct bound_by_fire_buff_t : public stat_buff_t
  {
    burnout_wave_t* damage;

    bound_by_fire_buff_t( const special_effect_t& e, action_t* a )
        : stat_buff_t( e.player, "bound_by_fire_and_blaze", e.driver(), e.item ),
        damage( debug_cast<burnout_wave_t*>( a ) )
    {
      set_default_value( e.driver()->effectN( 1 ).average( e.item ) );
      // Disable refresh on the buff to model in game behavior
      set_refresh_behavior( buff_refresh_behavior::DISABLED );
      // Set cooldown on buff to 0 to prevent triggering the cooldown on buff procs
      set_cooldown( 0_ms );
      // Set chance to prevent using the RPPM chance for buff applications
      set_chance( 1.01 );
    }

    void expire_override( int s, timespan_t d ) override
    {
      stat_buff_t::expire_override( s, d );
      // Debug cast to pass stack count through to the damage action
      damage -> buff_stacks = s;
      // Execute the debug cast
      damage -> execute();
    }
  };

  auto damage = create_proc_action<burnout_wave_t>( "burnout_wave", effect );
  auto buff = make_buff<bound_by_fire_buff_t>( effect, damage );

  special_effect_t* bound_by_fire_and_blaze = new special_effect_t( effect.player );
  bound_by_fire_and_blaze->source = effect.source;
  bound_by_fire_and_blaze->spell_id = effect.driver()->id();
  bound_by_fire_and_blaze->custom_buff = buff;
  bound_by_fire_and_blaze->cooldown_ = 0_ms;

  effect.player->special_effects.push_back( bound_by_fire_and_blaze );
  effect.proc_chance_ = 1.01;
  effect.custom_buff = buff;
  // since the on-use effect doesn't use the rppm, set to 0 so trinket expressions correctly determine it has a cooldown
  effect.ppm_ = 0;

  auto cb = new dbc_proc_callback_t( effect.player, *bound_by_fire_and_blaze );
  cb -> deactivate();

  buff->set_stack_change_callback( [ cb ](buff_t*, int, int new_) 
  {
    if( !new_ )
    {
      cb->deactivate();
    }
    else
    {
      cb->activate();
    }
  } );
}

void primal_ritual_shell( special_effect_t& effect )
{
  const auto& blessing = effect.player->sim->dragonflight_opts.primal_ritual_shell_blessing;
  {
    if ( util::str_compare_ci( blessing, "wind" ) )
    {
      // Wind Turtle's Blessing - Mastery Proc [390899]
      effect.stat_amount = effect.driver()->effectN( 5 ).average( effect.item );
      effect.spell_id = 390899;
      effect.stat = STAT_MASTERY_RATING;
      effect.name_str = util::tokenize_fn( effect.trigger()->name_cstr() );
    }
    else if ( util::str_compare_ci( blessing, "stone" ) )
    {
      // TODO: Implement Stone Turtle's Blessing - Absorb-Shield Proc [390655]
      effect.player->sim->error( "Stone Turtle's Blessing is not implemented yet" );
    }
    else if ( util::str_compare_ci( blessing, "flame" ) )
    {
      // Flame Turtle's Blessing - Fire Damage Proc [390835]
      effect.discharge_amount = effect.driver()->effectN( 3 ).average( effect.item );
      effect.spell_id = 390835;
      effect.name_str = util::tokenize_fn( effect.trigger()->name_cstr() );
    }
    else if ( util::str_compare_ci( blessing, "sea" ) )
    {
      // TODO: Implement Sea Turtle's Blessing - Heal Proc [390869]
      effect.player->sim->error( "Sea Turtle's Blessing is not implemented yet" );
    }
    else
    {
      throw std::invalid_argument( "Invalid string for dragonflight.primal_ritual_shell_blessing. Valid strings: wind,flame,sea,stone" );
    }
  }
  new dbc_proc_callback_t( effect.player, effect );
}

// Spineripper's Fang
// 383611 Driver
// 383612 Damage spell (damage value is in the driver effect)
// Double damage bonus from behind, not in spell data anywhere
void spinerippers_fang( special_effect_t& effect )
{
  auto action = create_proc_action<generic_proc_t>( "rip_spine", effect, "rip_spine",
                                                    effect.driver()->effectN( 1 ).trigger() );
  
  double damage = effect.driver()->effectN( 1 ).average( effect.item );
  if ( effect.player->position() == POSITION_BACK ||
       effect.player->position() == POSITION_RANGED_BACK )
  {
    damage *= 2.0;
  }
  action->base_dd_min = action->base_dd_max = damage;

  effect.execute_action = action;

  new dbc_proc_callback_t( effect.player, effect );
}

// Integrated Primal Fire
// 392359 Driver and fire damage impact
// 392376 Physical damage impact spell, damage in driver effect 2
void integrated_primal_fire( special_effect_t& effect )
{
  struct cataclysmic_punch_t : public generic_proc_t
  {
    cataclysmic_punch_t( const special_effect_t& e ) : generic_proc_t( e, "cataclysmic_punch", e.driver() )
    {
      // Physical Damage
      impact_action = create_proc_action<generic_proc_t>(
        "cataclysmic_punch_knock", e, "cataclysmic_punch_knock", e.driver()->effectN( 4 ).trigger() );
      impact_action->base_dd_min = impact_action->base_dd_max = e.driver()->effectN( 2 ).average( e.item );
      add_child( impact_action );
    }
  };

  effect.execute_action = create_proc_action<cataclysmic_punch_t>( "cataclysmic_punch", effect );
}

// Bushwhacker's Compass
// 383817 Driver
// 383818 Buffs
// North = Crit, South = Haste, East = Vers, West = Mastery
void bushwhackers_compass(special_effect_t& effect)
{
  struct cb_t : public dbc_proc_callback_t
  {
    std::vector<buff_t*> buffs;
    cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e )
    {
      auto the_path_to_survival_mastery = create_buff<stat_buff_t>(effect.player, "the_path_to_survival_mastery", effect.trigger() )
        -> add_stat( STAT_MASTERY_RATING, effect.trigger()->effectN( 5 ).average( effect.item ));

      auto the_path_to_survival_haste = create_buff<stat_buff_t>(effect.player, "the_path_to_survival_haste", effect.trigger() )
        -> add_stat( STAT_HASTE_RATING, effect.trigger()->effectN( 5 ).average( effect.item ));

      auto the_path_to_survival_crit = create_buff<stat_buff_t>(effect.player, "the_path_to_survival_crit", effect.trigger() )
        -> add_stat( STAT_CRIT_RATING, effect.trigger()->effectN( 5 ).average( effect.item ));

      auto the_path_to_survival_vers = create_buff<stat_buff_t>(effect.player, "the_path_to_survival_vers", effect.trigger() )
        -> add_stat( STAT_VERSATILITY_RATING, effect.trigger()->effectN( 5 ).average( effect.item ));

      buffs =
      {
        the_path_to_survival_mastery,
        the_path_to_survival_haste,
        the_path_to_survival_crit,
        the_path_to_survival_vers
      };
    }

    void execute( action_t* a, action_state_t* s ) override
    {
      dbc_proc_callback_t::execute( a, s );

      auto buff = effect.player -> sim -> rng().range( buffs.size() );
      buffs[ buff ] -> trigger();
    }
  };
  effect.buff_disabled = true;
  new cb_t( effect );
}

void seasoned_hunters_trophy( special_effect_t& effect )
{
  struct seasoned_hunters_trophy_cb_t : public dbc_proc_callback_t
  {
    buff_t *mastery, *haste, *crit;

    seasoned_hunters_trophy_cb_t( const special_effect_t& e, buff_t* m, buff_t* h, buff_t* c ) :
      dbc_proc_callback_t( e.player, e ), mastery( m ), haste( h ), crit( c )
    { }

    void execute( action_t* /* a */, action_state_t* /* s */ ) override
    {
      mastery->expire();
      haste->expire();
      crit->expire();

      auto pet_count = range::count_if( listener->active_pets, []( const pet_t* pet ) {
        return pet->type == PLAYER_PET;
      } );

      switch ( pet_count )
      {
        case 0:
          mastery->trigger();
          break;
        case 1:
          haste->trigger();
          break;
        default:
          crit->trigger();
          break;
      }
    }
  };

  buff_t* mastery = buff_t::find( effect.player, "hunter_versus_wild" );
  buff_t* haste = buff_t::find( effect.player, "hunters_best_friend" );
  buff_t* crit = buff_t::find( effect.player, "pack_mentality" );

  if ( !mastery )
  {
    mastery = create_buff<stat_buff_t>( effect.player, "hunter_versus_wild",
        effect.player->find_spell( 392271 ) )
      ->add_stat( STAT_MASTERY_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );
  }

  if ( !haste )
  {
    haste = create_buff<stat_buff_t>( effect.player, "hunters_best_friend",
        effect.player->find_spell( 392275 ) )
      ->add_stat( STAT_HASTE_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );
  }

  if ( !crit )
  {
    crit = create_buff<stat_buff_t>( effect.player, "pack_mentality",
        effect.player->find_spell( 392248 ) )
      ->add_stat( STAT_CRIT_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );
  }

  new seasoned_hunters_trophy_cb_t( effect, mastery, haste, crit );
}

// Gral's Discarded Tooth
// 374233 Driver
// 374249 Damage Driver
// 374250 Damage
void grals_discarded_tooth( special_effect_t& effect )
{
  auto missile = effect.trigger();
  auto trigger = missile -> effectN( 1 ).trigger();

  auto damage = create_proc_action<generic_aoe_proc_t>( "chill_of_the_depths", effect, "chill_of_the_depths", trigger -> id(), true);
  damage -> base_dd_min = damage -> base_dd_max = effect.driver()->effectN(1).average(effect.item);
  damage -> travel_speed = missile -> missile_speed();

  effect.execute_action = damage;
  new dbc_proc_callback_t( effect.player, effect );
}

// Static Charged Scale
// 391612 Driver
// 392127 Damage
// 392128 Buff
// Todo: Implement self damage?
void static_charged_scale(special_effect_t& effect)
{
  auto buff_spell = effect.player -> find_spell( 392128 );
  auto buff = create_buff<stat_buff_t>( effect.player, buff_spell );
  buff -> set_stat( STAT_HASTE_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );

  effect.custom_buff = buff;
  new dbc_proc_callback_t( effect.player, effect );
}

// Ruby Whelp Shell
// 383812 driver
// 389839 st damage
// 390234 aoe damage
// 389820 haste buff
// 383813 crit buff
// TODO what does the use effect actually do in game?
void ruby_whelp_shell(special_effect_t& effect)
{
  struct ruby_whelp_assist_cb_t : public dbc_proc_callback_t
  {
    action_t* shot;
    action_t* nova;
    stat_buff_t* haste;
    stat_buff_t* crit;

    ruby_whelp_assist_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e )
    {
      shot = create_proc_action<generic_proc_t>( "fire_shot", e, "fire_shot", 389839 );
      shot -> base_dd_min = shot -> base_dd_max = e.driver() -> effectN( 1 ).average( e.item );

      nova = create_proc_action<generic_aoe_proc_t>( "lobbing_fire_nova", e, "lobbing_fire_nova", 390234, true );
      nova -> base_dd_min = nova -> base_dd_max = e.driver() -> effectN( 2 ).average( e.item );

      haste = create_buff<stat_buff_t>( e.player, e.player -> find_spell( 389820 ) );
      haste -> set_stat( STAT_HASTE_RATING, e.driver() -> effectN( 6 ).average( e.item ) );

      crit = create_buff<stat_buff_t>( e.player, e.player -> find_spell( 383813 ) );
      crit -> set_stat( STAT_CRIT_RATING, e.driver() -> effectN( 5 ).average( e.item ) );
    }

    void execute( action_t*, action_state_t* s ) override
    {
      int choice = rng().range(0, 6);
      if ( choice == 0 )
        shot -> execute_on_target( s -> target );
      else if ( choice == 1 )
        nova -> execute_on_target( s -> target );
      else if ( choice == 2 )
        haste -> trigger();
      else if ( choice == 3 )
        crit -> trigger();
      // skip 2 heal possibilities
    }
  };

  new ruby_whelp_assist_cb_t( effect );
}

// Desperate Invoker's Codex
// 377465 procs the buff 382419 with every cast which reduces the proc action cd by 1s per stack
void desperate_invokers_codex( special_effect_t& effect )
{
  if ( unique_gear::create_fallback_buffs( effect, { "hatred" } ) )
    return;
  
  auto hatred =
      create_buff<buff_t>( effect.player, effect.player->find_spell( 382419 ) )->set_default_value_from_effect( 1, 1 );

  auto desperate_invocation          = find_special_effect( effect.player, 377465 );
  desperate_invocation->proc_flags_  = PF_ALL_DAMAGE;
  desperate_invocation->proc_flags2_ = PF2_CAST_DAMAGE;
  desperate_invocation->custom_buff  = hatred;
  auto cb                            = new dbc_proc_callback_t( effect.player, *desperate_invocation );
  cb->initialize();

  struct desperate_invocation_t : public generic_proc_t
  {
    buff_t* buff;
    cooldown_t* item_cd;
    cooldown_t* spell_cd;

    desperate_invocation_t( const special_effect_t& e, buff_t* b )
      : generic_proc_t( e, "desperate_invocation", e.driver() ),
        buff( b ),
        item_cd( e.player->get_cooldown( e.cooldown_name() ) ),
        spell_cd( e.player->get_cooldown( e.name() ) )
    {
      base_dd_min = base_dd_max = e.player->find_spell( 377465 )->effectN( 2 ).average( e.item );
    }

    void execute() override
    {
      generic_proc_t::execute();
      make_event( *sim, [ this ]() { item_cd->adjust( -timespan_t::from_seconds( buff->check_stack_value() ) ); } );
      spell_cd->adjust( -timespan_t::from_seconds( buff->check_stack_value() ) );
    }

    void activate() override
    {
      generic_proc_t::activate();

      // Cooldown reduction is removed when dropping combat in dungeon-style fight types
      sim->target_non_sleeping_list.register_callback( [ this ]( player_t* ) {
        if ( sim->target_non_sleeping_list.empty() )
          buff->expire();
      } );
    }
  };

  effect.execute_action = create_proc_action<desperate_invocation_t>( "Desperate Invocation", effect, hatred );
  hatred->set_stack_change_callback( [ effect ]( buff_t* b, int, int ) {
    if ( !b->at_max_stacks() )
    {
      effect.player->get_cooldown( effect.cooldown_name() )->adjust( -timespan_t::from_seconds( b->check_value() ) );
      effect.player->get_cooldown( effect.name() )->adjust( -timespan_t::from_seconds( b->check_value() ) );
    }
  } );
}

// Weapons
void bronzed_grip_wrappings( special_effect_t& effect )
{
  // TODO: currently whether the proc is damage or heal seems arbitrarily based on the spell that proc'd it, with no
  // discernable pattern as of yet.
  // * white melee hits will always proc damage
  // * white ranged hit will not proc anything
  // * heals will always proc heal
  // * yellow attacks and spells varies depending on the spell that procs it. some abilities always proc heal, others
  // always proc damage, yet others almost always proc only one type but have exceptions where it procs the other for
  // unknown reason.
  //
  // For now we err on the side of undersimming and disable all procs except for white hits. If this is not fixed by the
  // time it is available with raid launch, each spec will need to explicitly determine whether an ability will execute
  // the damage or heal on proc via effect_callback_t::register_callback_execute_function() to driver id 396442.
  struct bronzed_grip_wrappings_cb_t : public dbc_proc_callback_t
  {
    bronzed_grip_wrappings_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e ) {}

    void execute( action_t* a, action_state_t* s ) override
    {
      if ( s->target->is_sleeping() )
        return;

      if ( execute_fn )
      {
        ( *execute_fn )( this, a, s );
      }
      else
      {
        if ( !a->special )
          proc_action->execute_on_target( s->target );
      }
    }
  };

  auto amount = effect.driver()->effectN( 2 ).average( effect.item );

  effect.trigger_spell_id = effect.driver()->effectN( 2 ).trigger_spell_id();
  effect.spell_id = effect.driver()->effectN( 1 ).trigger_spell_id();
  effect.discharge_amount = amount;

  new bronzed_grip_wrappings_cb_t( effect );
}

void fang_adornments( special_effect_t& effect )
{
  effect.school = effect.driver()->get_school_type();
  effect.discharge_amount = effect.driver()->effectN( 1 ).average( effect.item );
  effect.override_result_es_mask |= RESULT_CRIT_MASK;
  effect.result_es_mask &= ~RESULT_CRIT_MASK;

  new dbc_proc_callback_t( effect.player, effect );
}

// Forgestorm
// 381698 Buff Driver
// 381699 Buff and Damage Driver
// 381700 Damage
void forgestorm( special_effect_t& effect )
{
  auto buff = buff_t::find( effect.player, "forgestorm_ignited" );
  if ( !buff )
  {
    auto buff_spell = effect.trigger();
    buff = create_buff<buff_t>( effect.player, buff_spell );
    
    auto forgestorm_damage = new special_effect_t( effect.player );
    forgestorm_damage->name_str = "forgestorm_ignited_damage";
    forgestorm_damage->item = effect.item;
    forgestorm_damage->spell_id = buff->data().id();
    forgestorm_damage->type = SPECIAL_EFFECT_EQUIP;
    forgestorm_damage->source = SPECIAL_EFFECT_SOURCE_ITEM;
    forgestorm_damage->execute_action = create_proc_action<generic_aoe_proc_t>(
      "forgestorm_ignited_damage", *forgestorm_damage, "forgestorm_ignited_damage", effect.player->find_spell( 381700 ), true );
    forgestorm_damage->execute_action->base_dd_min = forgestorm_damage->execute_action->base_dd_max
      = effect.player->find_spell( 381698 )->effectN( 1 ).average( effect.item );
    effect.player -> special_effects.push_back( forgestorm_damage );
    
    auto damage = new dbc_proc_callback_t( effect.player, *forgestorm_damage );
    damage->initialize();
    damage->deactivate();
 
    buff->set_stack_change_callback( [damage]( buff_t*, int, int new_ )
    {
      if ( new_ )
      {
        damage->activate();
      }
      else
      {
        damage->deactivate();
      }
    } );
  }
  effect.custom_buff = buff;
  new dbc_proc_callback_t( effect.player, effect );
}

// 394928 driver
// 397118 player buff
// 397478 unique target debuff
void neltharax( special_effect_t& effect )
{
  auto buff =
    create_buff<buff_t>( effect.player, "heavens_nemesis", effect.player->find_spell( 397118 ) )
      ->set_default_value_from_effect( 1 )
      ->add_invalidate( CACHE_ATTACK_SPEED );

  effect.player -> buffs.heavens_nemesis = buff;

  struct auto_cb_t : public dbc_proc_callback_t
  {
    buff_t* attack_speed;

    auto_cb_t( const special_effect_t& e, buff_t* buff ) : dbc_proc_callback_t( e.player, e ), attack_speed( buff )
    {}

    void execute( action_t*, action_state_t* s ) override
    {
      auto debuff = effect.player -> find_target_data( s -> target ) -> debuff.heavens_nemesis;
      if ( debuff -> check() )
      {
        // Increment the attack speed if target is our current mark.
        attack_speed -> trigger();
      }
      else
      {
        attack_speed -> expire();
        // Find a current mark if there is one and remove the mark on it.
        auto i = range::find_if( effect.player -> sim -> target_non_sleeping_list, [ this ]( const player_t* t ) {
          auto td = effect.player -> find_target_data( t );
          if ( td ) {
            auto debuff = td -> debuff.heavens_nemesis;
            if ( debuff -> check() )
            {
              debuff -> expire();
              return true;
            }
          }
          return false;
        });
        // Only set a new mark and start stacking the buff if there was no mark cleared on this impact.
        if ( i == effect.player -> sim -> target_non_sleeping_list.end() )
        {
          debuff -> trigger();
          attack_speed -> trigger();
        }
      }
    }
  };

  new auto_cb_t( effect, buff );
}

struct heavens_nemesis_initializer_t : public item_targetdata_initializer_t
{
  heavens_nemesis_initializer_t() : item_targetdata_initializer_t( 394928 ) {}

  void operator()( actor_target_data_t* td ) const override
  {
    if ( !find_effect( td->source ) )
    {
      td->debuff.heavens_nemesis = make_buff( *td, "heavens_nemesis_mark" )->set_quiet( true );
      return;
    }

    assert( !td->debuff.heavens_nemesis );
    td->debuff.heavens_nemesis = make_buff( *td, "heavens_nemesis_mark", td->source->find_spell( 397478 ) );
    td->debuff.heavens_nemesis->reset();
  }
};

// Armor
void assembly_guardians_ring( special_effect_t& effect )
{
  auto cb = new dbc_proc_callback_t( effect.player, effect );
  cb->initialize();

  cb->proc_buff->set_default_value( effect.driver()->effectN( 1 ).average( effect.item ) );
}

void assembly_scholars_loop( special_effect_t& effect )
{
  effect.discharge_amount = effect.driver()->effectN( 1 ).average( effect.item );

  new dbc_proc_callback_t( effect.player, effect );
}

void blue_silken_lining( special_effect_t& effect )
{
  auto buff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 387336 ) );
  buff->set_stat( STAT_MASTERY_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );
  buff->set_max_stack( 2 );

  // TODO: implement losing buff when hp < 90%
  effect.player->register_combat_begin( [ buff ]( player_t* ) { buff->trigger(); } );
}

void breath_of_neltharion( special_effect_t& effect )
{
  struct breath_of_neltharion_t : public generic_proc_t
  {
    breath_of_neltharion_t( const special_effect_t& e ) : generic_proc_t( e, "breath_of_neltharion", e.driver() )
    {
      channeled = true;
      harmful = false;

      tick_action = create_proc_action<generic_aoe_proc_t>(
          "breath_of_neltharion_tick", e, "breath_of_neltharion_tick", e.trigger() );
      tick_action->split_aoe_damage = false;
      tick_action->stats = stats;
      tick_action->dual = true;
    }

    bool usable_moving() const override
    {
      return true;
    }

    void execute() override
    {
      generic_proc_t::execute();

      event_t::cancel( player->readying );
      player->reset_auto_attacks( composite_dot_duration( execute_state ) );
    }

    void last_tick( dot_t* d ) override
    {
      bool was_channeling = player->channeling == this;

      generic_proc_t::last_tick( d );

      if ( was_channeling && !player->readying )
        player->schedule_ready( rng().gauss( sim->channel_lag, sim->channel_lag_stddev ) );
    }
  };

  effect.execute_action = create_proc_action<breath_of_neltharion_t>( "breath_of_neltharion", effect );
}

void coated_in_slime( special_effect_t& effect )
{
  auto mul = toxified_mul( effect.player );

  effect.player->passive.add_stat( util::translate_rating_mod( effect.driver()->effectN( 1 ).misc_value1() ),
                                   effect.driver()->effectN( 2 ).average( effect.item ) * mul );

  effect.trigger_spell_id = effect.driver()->effectN( 3 ).trigger_spell_id();
  effect.discharge_amount = effect.driver()->effectN( 3 ).average( effect.item ) * mul;

  new dbc_proc_callback_t( effect.player, effect );
}

void elemental_lariat( special_effect_t& effect )
{
  enum gem_type_e
  {
    AIR_GEM   = 0x1,
    EARTH_GEM = 0x2,
    FIRE_GEM  = 0x4,
    FROST_GEM = 0x8
  };

  // while part of this info is available in ItemSparse.db2, seems unnecessary to export another field to item_data.inc
  // just for this single effect
  using gem_name_type = std::pair<const char*, gem_type_e>;
  static constexpr gem_name_type gem_types[] =
  {
    { "Crafty Alexstraszite",   AIR_GEM   },
    { "Energized Malygite",     AIR_GEM   },
    { "Quick Ysemerald",        AIR_GEM   },
    { "Keen Neltharite",        AIR_GEM   },
    { "Forceful Nozdorite",     AIR_GEM   },
    { "Sensei's Alexstraszite", EARTH_GEM },
    { "Zen Malygite",           EARTH_GEM },
    { "Keen Ysemerald",         EARTH_GEM },
    { "Fractured Neltharite",   EARTH_GEM },
    { "Puissant Nozdorite",     EARTH_GEM },
    { "Deadly Alexstraszite",   FIRE_GEM  },
    { "Radiant Malygite",       FIRE_GEM  },
    { "Crafty Ysemerald",       FIRE_GEM  },
    { "Sensei's Neltharite",    FIRE_GEM  },
    { "Jagged Nozdorite",       FIRE_GEM  },
    { "Radiant Alexstraszite",  FROST_GEM },
    { "Stormy Malygite",        FROST_GEM },
    { "Energized Ysemerald",    FROST_GEM },
    { "Zen Neltharite",         FROST_GEM },
    { "Steady Nozdorite",       FROST_GEM },
  };

  // TODO: does having more of a type increase the chances of getting that buff?
  unsigned gems = 0;
  for ( const auto& item : effect.player->items )
  {
    for ( auto gem_id : item.parsed.gem_id )
    {
      if ( gem_id )
      {
        auto n = effect.player->dbc->item( gem_id ).name;
        auto it = range::find( gem_types, n, &gem_name_type::first );
        if ( it != std::end( gem_types ) )
          gems |= ( *it ).second;
      }
    }
  }

  if ( !gems )
    return;

  auto val = effect.driver()->effectN( 1 ).average( effect.item );
  auto dur = timespan_t::from_seconds( effect.driver()->effectN( 2 ).base_value() );
  std::vector<buff_t*> buffs;

  auto add_buff = [ &effect, gems, val, dur, &buffs ]( gem_type_e type, std::string suf, unsigned id, stat_e stat ) {
    if ( gems & type )
    {
      auto name = "elemental_lariat__empowered_" + suf;
      auto buff = buff_t::find( effect.player, name );
      if ( !buff )
      {
        buff = make_buff<stat_buff_t>( effect.player, name, effect.player->find_spell( id ) )
          ->add_stat( stat, val )
          ->set_duration( dur );
      }
      buffs.push_back( buff );
    }
  };

  add_buff( AIR_GEM, "air", 375342, STAT_HASTE_RATING );
  add_buff( EARTH_GEM, "earth", 375345, STAT_MASTERY_RATING );
  add_buff( FIRE_GEM, "flame", 375335, STAT_CRIT_RATING );
  add_buff( FROST_GEM, "frost", 375343, STAT_VERSATILITY_RATING );

  new dbc_proc_callback_t( effect.player, effect );

  effect.player->callbacks.register_callback_execute_function(
      effect.driver()->id(), [ buffs ]( const dbc_proc_callback_t* cb, action_t*, action_state_t* ) {
        range::for_each( buffs, []( buff_t* b ) { b->expire(); } );
        buffs[ cb->rng().range( buffs.size() ) ]->trigger();
      } );
}

void flaring_cowl( special_effect_t& effect )
{
  auto damage = create_proc_action<generic_aoe_proc_t>( "flaring_cowl", effect, "flaring_cowl", 377079, true );
  damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 1 ).average( effect.item );

  auto period = effect.trigger()->effectN( 1 ).period();
  effect.player->register_combat_begin( [ period, damage ]( player_t* p ) {
    make_repeating_event( p->sim, period, [ damage ]() { damage->execute(); } );
  } );
}

void thriving_thorns( special_effect_t& effect )
{
  auto mul = toxified_mul( effect.player );

  effect.player->passive.add_stat( STAT_STAMINA, effect.driver()->effectN( 2 ).average( effect.item ) * mul );

  // velocity & triggered missile reference is in 379395 for damage & 379405 for heal
  // TODO: implement heal
  auto damage_trg = effect.player->find_spell( 379395 );
  auto damage = create_proc_action<generic_proc_t>( "launched_thorns", effect, "launched_thorns",
                                                    damage_trg->effectN( 1 ).trigger() );
  damage->travel_speed = damage_trg->missile_speed();
  damage->base_dd_min = damage->base_dd_max = effect.driver()->effectN( 4 ).average( effect.item ) * mul;

  effect.execute_action = damage;

  new dbc_proc_callback_t( effect.player, effect );
}

// Broodkeepers Blaze
// 394452 Driver
// 394453 Damage & Buff
void broodkeepers_blaze(special_effect_t& effect)
{
  // callback to trigger debuff on specific schools
  struct broodkeepers_blaze_cb_t : public dbc_proc_callback_t
  {
    broodkeepers_blaze_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e ) {}

    void trigger( action_t* a, action_state_t* s ) override
    {
      if ( dbc::is_school( a->get_school(), SCHOOL_FIRE ) )
      {
        dbc_proc_callback_t::trigger( a , s );
      }
    }
  };

  auto dot = create_proc_action<generic_proc_t>("broodkeepers_blaze", effect, "broodkeepers_blaze", 394453 );
  dot->base_td = effect.driver()->effectN(1).average(effect.item);
  effect.execute_action = dot;
  new broodkeepers_blaze_cb_t( effect );
}

void amice_of_the_blue( special_effect_t& effect )
{
  auto damage = create_proc_action<generic_aoe_proc_t>( "amice_of_the_blue", effect, "amice_of_the_blue", effect.trigger() );
  damage->split_aoe_damage = true;
  damage->aoe = -1;
  effect.execute_action = damage;
  new dbc_proc_callback_t( effect.player, effect );
}

void rallied_to_victory( special_effect_t& effect )
{
  effect.custom_buff = create_buff<stat_buff_t>(effect.player, effect.trigger())
    ->add_stat(STAT_VERSATILITY_RATING, effect.driver()->effectN(1).average(effect.item));

  new dbc_proc_callback_t(effect.player, effect);
}

void deep_chill( special_effect_t& effect )
{
  effect.trigger_spell_id = 381006;
  effect.discharge_amount = effect.driver()->effectN(3).average(effect.item) * toxified_mul(effect.player);

  new dbc_proc_callback_t( effect.player, effect );
}

void potent_venom( special_effect_t& effect )
{
  double mult = toxified_mul(effect.player);
  double gain = effect.driver()->effectN(3).average(effect.item) * mult;
  double loss = std::abs(effect.driver()->effectN(2).average(effect.item)) * mult;

  struct potent_venom_t : public buff_t
  {
    stat_e gain = STAT_NONE;
    stat_e loss = STAT_NONE;

    potent_venom_t(player_t* p, util::string_view name, const spell_data_t* spell, const special_effect_t& effect) :
      buff_t(p, name, spell, effect.item)
    {
    }

    void reset() override
    {
      buff_t::reset();

      gain = STAT_NONE;
      loss = STAT_NONE;
    }
  };
  
  auto buff = create_buff<potent_venom_t>(effect.player, effect.driver()->effectN(3).trigger(), effect);
  buff->set_stack_change_callback( [effect, buff, gain, loss] ( buff_t*, int, int new_ )
    {
      static constexpr std::array<stat_e, 4> ratings = { STAT_VERSATILITY_RATING, STAT_MASTERY_RATING, STAT_HASTE_RATING, STAT_CRIT_RATING };
      if ( new_ )
      {
        buff->gain = util::highest_stat(effect.player, ratings);
        buff->loss = util::lowest_stat(effect.player, ratings);

        buff->player->stat_gain(buff->gain, gain);
        buff->player->stat_loss(buff->loss, loss);
      }
      else
      {
        buff->player->stat_loss(buff->gain, gain);
        buff->player->stat_gain(buff->loss, loss);
      }
    } );
  effect.custom_buff = buff;

  new dbc_proc_callback_t( effect.player, effect );
}

// Allied Wristguards of Companionship
// 395959 Driver
// 395965 Buff
void allied_wristguards_of_companionship( special_effect_t& effect )
{
  auto buff = create_buff<stat_buff_t>( effect.player, effect.trigger() );
  buff->add_stat( STAT_VERSATILITY_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );

  timespan_t period = effect.driver()->effectN( 1 ).period();

  effect.player->register_combat_begin( [ buff, period ]( player_t* p ) {
    int allies = p->sim->dragonflight_opts.allied_wristguards_allies;

    buff->trigger( p->sim->rng().range( 1, allies ) );

    make_repeating_event( p->sim, period, [ buff, p, allies ]() {
      if ( p->rng().roll( p->sim->dragonflight_opts.allied_wristguards_ally_leave_chance ) )
      {
        buff->expire();
        buff->trigger( p->sim->rng().range( 1, allies ) );
      }
      else if ( buff->check() < allies )
      {
        buff->expire();
        buff->trigger( allies );
      }
      else if ( buff->check() == allies )
      {
        return;
      }
    } );
  } );
}

// Allied Chestplate of Generosity
// 378134 Driver
// 378139 Buff 
// TODO: Potentially model allies recieving the buff as well? 
void allied_chestplate_of_generosity(special_effect_t& effect)
{
  auto buff = create_buff<stat_buff_t>( effect.player, effect.trigger() );
  buff -> set_default_value( effect.driver() -> effectN( 1 ).average( effect.item ) );

  effect.custom_buff = buff;

  new dbc_proc_callback_t( effect.player, effect );
}

// 395601 Driver
// 395603 Buff
void hood_of_surging_time( special_effect_t& effect )
{
  if ( unique_gear::create_fallback_buffs( effect, { "prepared_time" } ) )
    return;

  struct hood_of_surging_time_cb_t : public dbc_proc_callback_t
  {
    std::vector<int> target_list;

    hood_of_surging_time_cb_t( const special_effect_t& e ) : dbc_proc_callback_t( e.player, e ), target_list()
    {
    }

    void execute( action_t* a, action_state_t* s ) override
    {
      if ( range::contains( target_list, s->target->actor_spawn_index ) )
        return;

      dbc_proc_callback_t::execute( a, s );
      target_list.push_back( s->target->actor_spawn_index );
    }

    void reset() override
    {
      dbc_proc_callback_t::reset();
      target_list.clear();
    }
  };

  auto buff = buff_t::find( effect.player, "prepared_time" );
  if ( !buff )
  {
    buff =
        create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 395603 ) )
            ->add_stat( STAT_HASTE_RATING, effect.player->find_spell( 395603 )->effectN( 1 ).average( effect.item ) );
  }

  effect.custom_buff = buff;

  // TODO: Check if this procs off of periodic, First Strike did not
  effect.proc_flags_ = effect.proc_flags() & ~PF_PERIODIC;

  // The effect procs when damage actually happens.
  effect.proc_flags2_ = PF2_ALL_HIT;

  new hood_of_surging_time_cb_t( effect );

  // Create repeating combat even to easier simulate increased uptime on encounters where you can get first strike procs
  // while contuining your single target rotation
  effect.player->register_combat_begin( [ buff ]( player_t* ) {
    if ( buff->sim->dragonflight_opts.hood_of_surging_time_chance > 0.0 &&
         buff->sim->dragonflight_opts.hood_of_surging_time_period > 0_s &&
         buff->sim->dragonflight_opts.hood_of_surging_time_stacks > 0 )
    {
      make_repeating_event( buff->sim, buff->sim->dragonflight_opts.hood_of_surging_time_period, [ buff ] {
        if ( buff->rng().roll( buff->sim->dragonflight_opts.hood_of_surging_time_chance ) )
          buff->trigger( buff->sim->dragonflight_opts.hood_of_surging_time_stacks );
      } );
    }
  } );
}

}  // namespace items

namespace sets
{
void playful_spirits_fur( special_effect_t& effect )
{
  if ( !effect.player->sets->has_set_bonus( effect.player->specialization(), T29_PLAYFUL_SPIRITS_FUR, B2 ) )
    return;

  auto set_driver_id = effect.player->sets->set( effect.player->specialization(), T29_PLAYFUL_SPIRITS_FUR, B2 )->id();

  if ( effect.driver()->id() == set_driver_id )
  {
    effect.trigger_spell_id = 376932;

    new dbc_proc_callback_t( effect.player, effect );
  }
  else
  {
    unique_gear::find_special_effect( effect.player, set_driver_id )->discharge_amount +=
        effect.driver()->effectN( 1 ).average( effect.item );
  }
}

void horizon_striders_garments( special_effect_t& effect )
{
  if ( !effect.player->sets->has_set_bonus( effect.player->specialization(), T29_HORIZON_STRIDERS_GARMENTS, B2 ) )
    return;

  auto set_driver_id = effect.player->sets->set( effect.player->specialization(), T29_HORIZON_STRIDERS_GARMENTS, B2 )->id();

  if (effect.driver()->id() == set_driver_id)
  {
    effect.proc_flags2_ = PF2_CRIT;

    new dbc_proc_callback_t(effect.player, effect);
  }
  else
  {
    auto buff = debug_cast<stat_buff_t*>(buff_t::find(effect.player, effect.name()));
    if (!buff)
    {
      buff = create_buff<stat_buff_t>(effect.player, effect.player->find_spell(377143));
    }
    buff->add_stat(STAT_HASTE_RATING, effect.driver()->effectN(1).average(effect.item));
    auto driver = unique_gear::find_special_effect(effect.player, set_driver_id);
    driver->custom_buff = buff;
  }
}

// Damage buff - 388061
// TODO: Healing buff - 388064
void azureweave_vestments( special_effect_t& effect )
{
  if ( !effect.player->sets->has_set_bonus( effect.player->specialization(), T29_AZUREWEAVE_VESTMENTS, B2 ) )
    return;

  auto set_driver_id = effect.player->sets->set( effect.player->specialization(), T29_AZUREWEAVE_VESTMENTS, B2 )->id();

  if ( effect.driver()->id() == set_driver_id )
  {
    // effect.proc_flags2_ = PF2_ALL_HIT | PF2_PERIODIC_DAMAGE;

    new dbc_proc_callback_t( effect.player, effect );
  }
  else
  {
    auto buff = buff_t::find( effect.player, effect.name() );
    if ( !buff )
    {
      buff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 388061 ) )
                 ->add_stat( STAT_INTELLECT, effect.driver()->effectN( 1 ).average( effect.item ) );
    }
    auto driver         = unique_gear::find_special_effect( effect.player, set_driver_id );
    driver->custom_buff = buff;
  }
}

// building stacks - 387141 (moment_of_time)
// haste - 387142 (unleashed_time)
void woven_chronocloth( special_effect_t& effect )
{
  if ( !effect.player->sets->has_set_bonus( effect.player->specialization(), T29_WOVEN_CHRONOCLOTH, B2 ) )
    return;

  auto set_driver_id = effect.player->sets->set( effect.player->specialization(), T29_WOVEN_CHRONOCLOTH, B2 )->id();

  if ( effect.driver()->id() == set_driver_id )
  {
    new dbc_proc_callback_t( effect.player, effect );
  }
  else
  {
    auto haste_buff = buff_t::find( effect.player, "unleashed_time" );
    if ( !haste_buff )
    {
      haste_buff = create_buff<stat_buff_t>( effect.player, effect.player->find_spell( 387142 ) )
                       ->add_stat( STAT_HASTE_RATING, effect.driver()->effectN( 1 ).average( effect.item ) );
    }

    auto buff = buff_t::find( effect.player, effect.name() );
    if ( !buff )
    {
      buff = create_buff<buff_t>( effect.player, effect.player->find_spell( 387141 ) )
                 ->set_expire_at_max_stack( true )
                 ->set_stack_change_callback( [ haste_buff ]( buff_t* b, int, int /*new_*/ ) {
                   if ( b->at_max_stacks() )
                   {
                     haste_buff->trigger();
                   }
                 } );
    }
    auto driver         = unique_gear::find_special_effect( effect.player, set_driver_id );
    driver->custom_buff = buff;
  }
}

void raging_tempests( special_effect_t& effect )
{
  auto check_set = [ effect ]( set_bonus_e b ) {
    return effect.player->sets->has_set_bonus( effect.player->specialization(), T29_RAGING_TEMPESTS, b ) &&
           effect.player->sets->set( effect.player->specialization(), T29_RAGING_TEMPESTS, b )->id() ==
               effect.driver()->id();
  };

  if ( check_set( B2 ) )
  {
    static constexpr std::array<stat_e, 4> ratings =
        { STAT_VERSATILITY_RATING, STAT_MASTERY_RATING, STAT_HASTE_RATING, STAT_CRIT_RATING };

    auto buff = create_buff<stat_buff_t>( effect.player, effect.driver() );
    buff->set_constant_behavior( buff_constant_behavior::ALWAYS_CONSTANT );

    auto val = effect.driver()->effectN( 1 ).average( effect.player );

    // the amount is set when you eqiup the item and does not dynamically update. To account for any shenanigans with
    // temporary buffs during equip, instead of implementing as a passive stat bonus we create a buff to trigger on
    // combat start, accounting for anything in the precombat apl.
    effect.player->register_combat_begin( [ buff, effect, val ]( player_t* p ) {
      buff->set_stat( util::highest_stat( p, ratings ), val );
      buff->trigger();
    } );
  }
  else if ( check_set( B4 ) )
  {
    auto counter = create_buff<buff_t>( effect.player, effect.player->find_spell( 390529 ) )
      ->set_expire_at_max_stack( true );

    auto driver = new special_effect_t( effect.player );
    driver->type = SPECIAL_EFFECT_EQUIP;
    driver->source = SPECIAL_EFFECT_SOURCE_ITEM;
    driver->spell_id = 390579;
    driver->duration_ = 0_ms;
    driver->execute_action =
        create_proc_action<generic_proc_t>( "spark_of_the_primals", *driver, "spark_of_the_primals", 392038 );
    effect.player->special_effects.push_back( driver );

    auto cb = new dbc_proc_callback_t( effect.player, *driver );
    cb->initialize();
    cb->deactivate();

    counter->set_stack_change_callback( [ cb ]( buff_t*, int, int new_ ) {
      if ( !new_ )
        cb->activate();
    } );

    effect.player->callbacks.register_callback_execute_function(
        driver->spell_id, [ cb ]( const dbc_proc_callback_t*, action_t* a, action_state_t* ) {
          cb->proc_action->execute_on_target( a->target );
          cb->deactivate();
        } );

    effect.player->register_on_kill_callback( [ counter ]( player_t* ) {
      counter->trigger();
    } );
  }
}
}  // namespace sets

void register_special_effects()
{
  // Food
  // Phials
  register_special_effect( 374000, consumables::iced_phial_of_corrupting_rage );
  register_special_effect( 371386, consumables::phial_of_charged_isolation );
  register_special_effect( 371339, consumables::phial_of_elemental_chaos );
  register_special_effect( 373257, consumables::phial_of_glacial_fury );
  register_special_effect( 370652, consumables::phial_of_static_empowerment );

  // Potion
  register_special_effect( 372046, consumables::bottled_putrescence );
  register_special_effect( { 371149, 371151, 371152 }, consumables::chilled_clarity );
  register_special_effect( { 371024, 371028 }, consumables::elemental_power );  // normal & ultimate
  register_special_effect( 370816, consumables::shocking_disclosure );

  // Enchants
  register_special_effect( { 390164, 390167, 390168 }, enchants::writ_enchant() );  // burning writ
  register_special_effect( { 390172, 390183, 390190 }, enchants::writ_enchant() );  // earthen writ
  register_special_effect( { 390243, 390244, 390246 }, enchants::writ_enchant() );  // frozen writ
  register_special_effect( { 390248, 390249, 390251 }, enchants::writ_enchant() );  // wafting writ
  register_special_effect( { 390215, 390217, 390219 },
                           enchants::writ_enchant( STAT_STR_AGI_INT ) );     // sophic writ
  register_special_effect( { 390346, 390347, 390348 },
                           enchants::writ_enchant( STAT_BONUS_ARMOR ) );            // earthen devotion
  register_special_effect( { 390222, 390227, 390229 },
                           enchants::writ_enchant( STAT_STR_AGI_INT ) );     // sophic devotion
  register_special_effect( { 390351, 390352, 390353 }, enchants::frozen_devotion );
  register_special_effect( { 390358, 390359, 390360 }, enchants::wafting_devotion );

  register_special_effect( { 386260, 386299, 386305 }, enchants::completely_safe_rockets );
  register_special_effect( { 386156, 386157, 386158 }, enchants::high_intensity_thermal_scanner );
  register_special_effect( { 385765, 385886, 385892 }, enchants::gyroscopic_kaleidoscope );
  register_special_effect( { 385939, 386127, 386136 }, enchants::projectile_propulsion_pinion );
  register_special_effect( { 387302, 387303, 387306 }, enchants::temporal_spellthread );


  // Trinkets
  register_special_effect( 384636, items::consume_pods );                            // burgeoning seed
  register_special_effect( 376636, items::idol_of_the_aspects( "neltharite" ) );     // idol of the earth warder
  register_special_effect( 376638, items::idol_of_the_aspects( "ysemerald" ) );      // idol of the dreamer
  register_special_effect( 376640, items::idol_of_the_aspects( "malygite" ) );       // idol of the spellweaver
  register_special_effect( 376642, items::idol_of_the_aspects( "alexstraszite" ) );  // idol of the lifebinder
  register_special_effect( 384615, items::darkmoon_deck_dance );
  register_special_effect( 382957, items::darkmoon_deck_inferno );
  register_special_effect( 386624, items::darkmoon_deck_rime );
  register_special_effect( 384532, items::darkmoon_deck_watcher );
  register_special_effect( 375626, items::alacritous_alchemist_stone );
  register_special_effect( 383751, items::bottle_of_spiraling_winds );
  register_special_effect( 396391, items::conjured_chillglobe );
  register_special_effect( 388931, items::globe_of_jagged_ice );
  register_special_effect( 383941, items::irideus_fragment );
  register_special_effect( 383798, items::emerald_coachs_whistle );
  register_special_effect( 381471, items::erupting_spear_fragment );
  register_special_effect( 383920, items::furious_ragefeather );
  register_special_effect( 388603, items::idol_of_pure_decay );
  register_special_effect( 384191, items::shikaari_huntress_arrowhead );
  register_special_effect( 377466, items::spiteful_storm );
  register_special_effect( 381768, items::spoils_of_neltharus, true );
  register_special_effect( 397399, items::voidmenders_shadowgem );
  register_special_effect( 375844, items::sustaining_alchemist_stone );
  register_special_effect( 385884, items::timebreaching_talon );
  register_special_effect( 385902, items::umbrelskuls_fractured_heart );
  register_special_effect( 377456, items::way_of_controlled_currents );
  register_special_effect( 377452, items::whispering_incarnate_icon );
  register_special_effect( 384112, items::the_cartographers_calipers );
  register_special_effect( 377454, items::rumbling_ruby );
  register_special_effect( 377453, items::storm_eaters_boon );
  register_special_effect( 377449, items::decoration_of_flame );
  register_special_effect( 377463, items::manic_grieftorch );
  register_special_effect( 377457, items::alltotem_of_the_master );
  register_special_effect( 388559, items::tome_of_unstable_power );
  register_special_effect( 383781, items::algethar_puzzle_box );
  register_special_effect( 382119, items::frenzying_signoll_flare );
  register_special_effect( 386175, items::idol_of_trampling_hooves );
  register_special_effect( 386692, items::dragon_games_equipment);
  register_special_effect( 397400, items::bonemaws_big_toe );
  register_special_effect( 381705, items::mutated_magmammoth_scale );
  register_special_effect( 382139, items::homeland_raid_horn );
  register_special_effect( 383926, items::blazebinders_hoof );
  register_special_effect( 390764, items::primal_ritual_shell );
  register_special_effect( 383611, items::spinerippers_fang );
  register_special_effect( 392359, items::integrated_primal_fire );
  register_special_effect( 383817, items::bushwhackers_compass );
  register_special_effect( 392237, items::seasoned_hunters_trophy );
  register_special_effect( 374233, items::grals_discarded_tooth );
  register_special_effect( 391612, items::static_charged_scale );
  register_special_effect( 383812, items::ruby_whelp_shell );
  register_special_effect( 377464, items::desperate_invokers_codex, true );
  

  // Weapons
  register_special_effect( 396442, items::bronzed_grip_wrappings );  // bronzed grip wrappings embellishment
  register_special_effect( 377708, items::fang_adornments );         // fang adornments embellishment
  register_special_effect( 381698, items::forgestorm );              // Forgestorm Weapon
  register_special_effect( 394928, items::neltharax );               // Neltharax, Enemy of the Sky


  // Armor
  register_special_effect( 397038, items::assembly_guardians_ring );
  register_special_effect( 397040, items::assembly_scholars_loop );
  register_special_effect( 387335, items::blue_silken_lining );    // blue silken lining embellishment
  register_special_effect( 385520, items::breath_of_neltharion );  // breath of neltharion tinker
  register_special_effect( 378423, items::coated_in_slime );
  register_special_effect( 375323, items::elemental_lariat );
  register_special_effect( 381424, items::flaring_cowl );
  register_special_effect( 379396, items::thriving_thorns );
  register_special_effect( 394452, items::broodkeepers_blaze );
  register_special_effect( 387144, items::amice_of_the_blue );
  register_special_effect( 378134, items::rallied_to_victory );
  register_special_effect( 380717, items::deep_chill );
  register_special_effect( 379985, items::potent_venom );
  register_special_effect( 395959, items::allied_wristguards_of_companionship );
  register_special_effect( 378134, items::allied_chestplate_of_generosity );
  register_special_effect( 395601, items::hood_of_surging_time, true );
  
  // Sets
  register_special_effect( { 393620, 393982 }, sets::playful_spirits_fur );
  register_special_effect( { 393983, 393762 }, sets::horizon_striders_garments );
  register_special_effect( { 393987, 393768 }, sets::azureweave_vestments );
  register_special_effect( { 393993, 393818 }, sets::woven_chronocloth );
  register_special_effect( { 389987, 389498, 391117 }, sets::raging_tempests );

  // Disabled
  register_special_effect( 382108, DISABLED_EFFECT );  // burgeoning seed
  register_special_effect( 382958, DISABLED_EFFECT );  // df darkmoon deck shuffler
  register_special_effect( 382913, DISABLED_EFFECT );  // bronzescale sigil (faster shuffle)
  register_special_effect( 383336, DISABLED_EFFECT );  // azurescale sigil (shuffle greatest to least)
  register_special_effect( 383333, DISABLED_EFFECT );  // emberscale sigil (no longer shuffles even cards)
  register_special_effect( 383337, DISABLED_EFFECT );  // jetscale sigil (no longer shuffles on Ace)
  register_special_effect( 383339, DISABLED_EFFECT );  // sagescale sigil (shuffle on jump) NYI
  register_special_effect( 378758, DISABLED_EFFECT );  // toxified embellishment
  register_special_effect( 371700, DISABLED_EFFECT );  // potion absorption inhibitor embellishment
  register_special_effect( 381766, DISABLED_EFFECT );  // spoils of neltharius shuffler
  register_special_effect( 383931, DISABLED_EFFECT );  // globe of jagged ice counter
  register_special_effect( 389843, DISABLED_EFFECT );  // ruby whelp shell (on-use)
  register_special_effect( 377465, DISABLED_EFFECT );  // Desperate Invocation (cdr proc)
  register_special_effect( 398396, DISABLED_EFFECT );  // emerald coach's whistle on-use
}

void register_target_data_initializers( sim_t& sim )
{
  sim.register_target_data_initializer( items::awakening_rime_initializer_t() );
  sim.register_target_data_initializer( items::skewering_cold_initializer_t() );
  sim.register_target_data_initializer( items::spiteful_storm_initializer_t() );
  sim.register_target_data_initializer( items::heavens_nemesis_initializer_t() );
}

void register_hotfixes()
{
}

// check and return multiplier for toxified armor patch
// TODO: spell data seems to indicate you can have up to 4 stacks. currently implemented as a simple check
double toxified_mul( player_t* player )
{
  if ( auto toxic = unique_gear::find_special_effect( player, 378758 ) )
    return 1.0 + toxic->driver()->effectN( 2 ).percent();

  return 1.0;
}

// check and return multiplier for potion absorption inhibitor
double inhibitor_mul( player_t* player )
{
  int count = 0;

  for ( const auto& item : player->items )
    if ( range::contains( item.parsed.special_effects, 371700U, &special_effect_t::spell_id ) )
      count++;

  return 1.0 + count * player->find_spell( 371700 )->effectN( 1 ).percent();
}
}  // namespace unique_gear::dragonflight
