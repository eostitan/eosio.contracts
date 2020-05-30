#pragma once

#include <math.h>
#include <eosio/crypto.hpp>

namespace eosiosystem {

   using namespace eosio;

   struct account_cpu {
     name a; // account
     uint64_t u; // cpu_usage_us
   };

   struct [[eosio::table("resusagedata"), eosio::contract("eosio.system")]] datasets
   {
      uint64_t id;
      checksum256 hash;
      std::vector<account_cpu> data; // hash of each individual data submission
      uint64_t primary_key() const { return (id); }
      checksum256 by_hash() const { return hash; }
   };

   struct [[eosio::table("resourceconf"), eosio::contract("eosio.system")]] resource_config_state
   {
      uint32_t period_seconds = 86400; // how many seconds in each period, low numbers used for testing
      uint16_t oracle_consensus_threshold; // how many oracles are required for mode to trigger distribution
      uint16_t dataset_max_size; // how many individual accounts are submitted at once
      time_point_sec period_start; // when the period currently open for reporting started
      std::vector<name> submitting_oracles; // oracles which have sent at least one dataset for this period
   };

   struct [[eosio::table("ressources"), eosio::contract("eosio.system")]] sources
   {
      name account;
      uint64_t primary_key() const { return (account.value); }
   };

   struct [[eosio::table("ressysusage"), eosio::contract("eosio.system")]] system_usage
   {
      name source; // oracle source
      uint64_t total_cpu_us;
      uint64_t total_net_words;
      uint64_t allocated_cpu = 0; // how much has been allocated to individual accounts
      std::vector<checksum256> submission_hash_list; // hash of each individual data submission
      uint64_t primary_key() const { return (source.value); }
   };

   struct [[eosio::table("resaccpay"), eosio::contract("eosio.system")]] account_pay
   {
      name account; //Worbli account consuming the resource
      asset payout; //WBI asset to pay for this period
      time_point_sec timestamp;
      uint64_t primary_key() const { return (account.value); }
   };

   struct [[eosio::table, eosio::contract("eosio.system")]] feature_toggle
   {
      name feature;
      bool active = false;
      uint64_t primary_key() const { return (feature.value); }
   };

   typedef eosio::singleton<"resourceconf"_n, resource_config_state> resource_config_singleton;
   typedef eosio::multi_index<"ressources"_n, sources> sources_table;
   typedef eosio::multi_index<"ressysusage"_n, system_usage> system_usage_table;
//   typedef eosio::multi_index<"resaccusage"_n, account_usage> account_usage_table;
   typedef eosio::multi_index<"resaccpay"_n, account_pay> account_pay_table;
   typedef eosio::multi_index<"feattoggle"_n, feature_toggle> feature_toggle_table;
   typedef eosio::multi_index<"resusagedata"_n, datasets, 
            indexed_by<"hash"_n, const_mem_fun<datasets, checksum256, &datasets::by_hash>>> datasets_table;
}

namespace worbli {

  // calculate a moving average
  static float calcMA(float sum, uint8_t timeperiod, float newVal)
  {
    auto rslt = sum + newVal;
    return rslt / (timeperiod);
  }

  // calculate an exponential moving average
  static float calcEMA(float previousAverage, int timePeriod, float newVal)
  {
    auto mult = 2.0 / (timePeriod + 1.0);
    auto rslt = (newVal - previousAverage) * mult + previousAverage;
    return rslt;
  }

  static float get_c(float x)
  { // model C[x] = -x * ln(x) * exp(1)
    float p1 = -x;
    float p2 = log(float(x));
    float p3 = exp(1);
    return p1 * p2 * p3;
  }

  static float round(float var)
  {
    //array of chars to store number as a string.
    char str[40];
    // Print in string the value of var with 4 decimal points
    sprintf(str, "%.4f", var);
    // scan string value in var
    sscanf(str, "%f", &var);
    return var;
  }

}