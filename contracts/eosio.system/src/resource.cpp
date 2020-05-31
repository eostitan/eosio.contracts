#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>


namespace eosiosystem {

    void system_contract::settotal(uint64_t total_cpu_us, uint64_t total_net_words, time_point_sec timestamp)
    {

//        check(!_resource_config_state.locked, "prior collection period is still open");
        check(total_cpu_us > 0, "cpu measurement must be greater than 0");
        check(total_net_words > 0, "net measurement must be greater than 0");

        system_usage_history_table u_t(get_self(), get_self().value);
        auto itr = u_t.end();
        itr--;

        // Initial Inflation
        float VT = 0.0185; // Initial Inflation Constant for Value Transfer
        float MP = 0.2947; // TO-DO: move these 2 constants to the config talbe for easier adjusting?

        uint64_t draglimit = _resource_config_state.emadraglimit;
        uint64_t day_count = itr->daycount + 1;

        float previousAverageCPU = itr->ma_cpu;
        float previousAverageNET = itr->ma_net;


        uint64_t system_max_cpu = static_cast<uint64_t>(_gstate.max_block_cpu_usage) * 2 * 60 * 60 * 24;
        check( total_cpu_us <= system_max_cpu, "measured cpu usage is greater than system total");
        float usage_cpu = static_cast<float>(total_cpu_us) / system_max_cpu;

        if(usage_cpu < 0.01) {
            usage_cpu = 0.01;
        }

        uint64_t system_max_net = static_cast<uint64_t>(_gstate.max_block_net_usage) * 2 * 60 * 60 * 24;
        check( total_net_words * 8 <= system_max_net, "measured net usage is greater than system total");
        float usage_net = static_cast<float>(total_net_words * 8) / system_max_net;

        if(usage_net < 0.01) {
            usage_net = 0.01;
        }

        float net_percent_total = usage_net / (usage_net + usage_cpu);
        float cpu_percent_total = usage_cpu / (usage_net + usage_cpu);

         print(" :: system_max_net: ");
         print(std::to_string(system_max_net));

         print(" :: system_max_cpu: ");
         print(std::to_string(system_max_cpu));

         print(" :: usage_cpu: ");
         print(std::to_string(usage_cpu));

        float ma_cpu_total = 0.0;
        float ma_net_total = 0.0;

        itr = u_t.end();
        for (int i = 1; i < draglimit; i++)
        {
            itr--;
            ma_cpu_total += itr->use_cpu;
            ma_net_total += itr->use_net;

            if (itr == u_t.begin())
            {
                break;
            }
        }

        // calculate period for moving averages during bootstrap period
        uint8_t period = day_count < draglimit ? day_count + 1 : draglimit;

        print(" :: period: ");
        print(std::to_string(period));

        float UTIL_CPU_MA = worbli::calcMA(ma_cpu_total, period, usage_cpu);
        float UTIL_NET_MA = worbli::calcMA(ma_net_total, period, usage_net);

        float UTIL_CPU_EMA;
        float UTIL_NET_EMA;

        uint64_t pk = u_t.available_primary_key();

        // use simple moving average until we reach draglimit samples
        if (pk >= draglimit)
        {
            UTIL_CPU_EMA = worbli::calcEMA(previousAverageCPU, draglimit, usage_cpu);
            UTIL_NET_EMA = worbli::calcEMA(previousAverageNET, draglimit, usage_net);
        }
        else
        {
            UTIL_CPU_EMA = UTIL_CPU_MA;
            UTIL_NET_EMA = UTIL_NET_MA;
        }
        float UTIL_TOTAL_EMA = (UTIL_CPU_EMA + UTIL_NET_EMA) / 2;

        if(UTIL_TOTAL_EMA < 0.01) {
            UTIL_TOTAL_EMA = 0.01;
        }

        if(UTIL_TOTAL_EMA == 1.0) {
            UTIL_TOTAL_EMA = 0.99;
        }

        float inflation = (1 - UTIL_TOTAL_EMA) / (1 - UTIL_TOTAL_EMA - worbli::get_c(UTIL_TOTAL_EMA) * VT) - 1;

        float BP_U = MP * worbli::get_c(UTIL_TOTAL_EMA);
        float Upaygross = pow((1 + inflation), (1 - BP_U)) - 1;
        float Bppay = inflation - Upaygross;

        print(" :: UTIL_TOTAL_EMA: ");
        print(std::to_string(UTIL_TOTAL_EMA));

        print(" :: inflation: ");
        print(std::to_string(inflation));

        print(" :: BP_U: ");
        print(std::to_string(BP_U));

        print(" :: Upaygross: ");
        print(std::to_string(Upaygross));

        print(" :: Bppay: ");
        print(std::to_string(Bppay));

        const asset token_supply = eosio::token::get_supply(token_account, core_symbol().code());

        // Inflation waterfall
        float Min_Upaynet = inflation * UTIL_TOTAL_EMA;

        print(" :: Min_Upaynet: ");
        print(std::to_string(Min_Upaynet));

        float Waterfall_bp = inflation * (1 - UTIL_TOTAL_EMA);

        print(" :: Waterfall_bp: ");
        print(std::to_string(Waterfall_bp));

        float Bppay_final = fmin(Bppay, Waterfall_bp);

        print(" :: Bppay_final: ");
        print(std::to_string(Bppay_final));

        float Uppaynet = inflation - Bppay_final;

        print(" :: Uppaynet: ");
        print(std::to_string(Uppaynet));

        double Daily_i_U = pow(1 + inflation, static_cast<double>(1) / 365) - 1;

        print(" :: Daily_i_U: ");
        print(std::to_string(Daily_i_U));

        float utility_daily = (Uppaynet / inflation) * Daily_i_U;                               //allocate proportionally to Utility
        float bppay_daily = (Bppay_final / inflation) * Daily_i_U;                            //allocate proportionally to BPs

        float cpu_daily = cpu_percent_total * utility_daily;
        float net_daily = utility_daily - cpu_daily;


        // calculate inflation amount
        auto utility_tokens = static_cast<int64_t>( (cpu_daily * double(token_supply.amount)));
        auto bppay_tokens = static_cast<int64_t>( ((bppay_daily) * double(token_supply.amount)));
        auto net_tokens = static_cast<int64_t>( (net_daily * double(token_supply.amount)));

        print(" :: utility_tokens: ");
        print(std::to_string(utility_tokens));

        u_t.emplace(get_self(), [&](auto &h) {
            h.id = pk;
            h.timestamp = timestamp;
            h.daycount = day_count;
            h.total_cpu_us = total_cpu_us;
            h.total_net_words = total_net_words;
            h.net_percent_total = net_percent_total;
            h.cpu_percent_total = cpu_percent_total;
            h.use_cpu = usage_cpu;
            h.use_net = usage_net;
            h.ma_cpu = UTIL_CPU_MA;
            h.ma_net = UTIL_NET_MA;
            h.ema_cpu = UTIL_CPU_EMA;
            h.ema_net = UTIL_NET_EMA;
            h.ema_util_total = UTIL_TOTAL_EMA;
            h.utility = Upaygross;
            h.utility_daily = utility_daily;
            h.bppay = Bppay;
            h.bppay_daily = bppay_daily;
            h.inflation = inflation;
            h.inflation_daily = Daily_i_U;
            h.utility_tokens = asset(utility_tokens, core_symbol() );
            h.bppay_tokens = asset(bppay_tokens, core_symbol() );
            h.net_tokens = asset(net_tokens, core_symbol() );
        });

/*        

        // open contract for user stats
        _resource_config_state.locked = true;

        // reset totals for user stats
        _resource_config_state.allocated_cpu = 0.0;
        _resource_config_state.allocated_net = 0.0;
        _resource_config_state.allocated_total = 0.0;
        _resource_config_state.utility_cpu_pay = asset( 0, core_symbol() );

*/
    }


    ACTION system_contract::initresource(uint16_t dataset_max_size, uint16_t oracle_consensus_threshold, time_point_sec period_start)
    {
        require_auth(get_self());

        _resource_config_state.dataset_max_size = dataset_max_size;
        _resource_config_state.oracle_consensus_threshold = oracle_consensus_threshold;
        _resource_config_state.period_start = period_start;

        system_usage_history_table u_t(get_self(), get_self().value);
        if (u_t.begin() == u_t.end()) {
            uint64_t pk = u_t.available_primary_key();
            u_t.emplace(get_self(), [&](auto &u) {
                u.id = pk;
                u.timestamp = period_start;
                u.use_cpu = 0;
                u.use_net = 0;
                u.daycount = 0;
                u.ma_cpu = 0;
                u.ma_net = 0;
                u.ema_cpu = 0;
                u.ema_net = 0;
                u.utility_daily = 0;
                u.bppay_daily = 0;
            });
        }
    }

    // sets total resources used by system (for calling oracle)
    // this must be called by oracle before adding individual cpu usage
    ACTION system_contract::settotalusg(name source, uint64_t total_cpu_us, uint64_t total_net_words, time_point_sec timestamp)
    {
        require_auth(source);
//        check(is_source(source) == true, "not authorized to execute this action");

        check(total_cpu_us > 0, "cpu measurement must be greater than 0");
        check(total_net_words > 0, "net measurement must be greater than 0");

        // todo - check timestamp and advance _resource_config_state if necessary

        check(_resource_config_state.period_start == timestamp, "timestamp does not match period_start");

        // check submissions are within system limits
        uint64_t system_max_cpu = static_cast<uint64_t>(_gstate.max_block_cpu_usage) * 2 * 60 * 60 * 24;
        check( total_cpu_us <= system_max_cpu, "measured cpu usage is greater than system total");
        uint64_t system_max_net = static_cast<uint64_t>(_gstate.max_block_net_usage) * 2 * 60 * 60 * 24;
        check( total_net_words * 8 <= system_max_net, "measured net usage is greater than system total");

/*
        float usage_cpu = static_cast<float>(total_cpu_us) / system_max_cpu;

        if(usage_cpu < 0.01) {
            usage_cpu = 0.01;
        }
        float usage_net = static_cast<float>(total_net_words * 8) / system_max_net;

        if(usage_net < 0.01) {
            usage_net = 0.01;
        }

        float net_percent_total = usage_net / (usage_net + usage_cpu);
        float cpu_percent_total = usage_cpu / (usage_net + usage_cpu);
*/

        system_usage_table u_t(get_self(), get_self().value);
        auto itr = u_t.find(source.value);

        check(itr == u_t.end(), "total already set");

        // hash submitted data
        std::string datatext = std::to_string(total_cpu_us) + std::to_string(total_net_words);
        checksum256 hash = sha256(datatext.c_str(), datatext.size());
        std::vector<metric> data {
            {"cpu.us"_n, total_cpu_us},
            {"net.words"_n, total_net_words}
        };

        // add data and hash to table if not already present
        datasets_table d_t(get_self(), get_self().value);
        auto dt_hash_index = d_t.get_index<"hash"_n>();
        auto dt_itr = dt_hash_index.find(hash);
        if (dt_itr->hash != hash) {
            d_t.emplace(get_self(), [&](auto& t) {
                t.id = d_t.available_primary_key();
                t.hash = hash;
                t.data = data;
            });
        }

        // add totals data
        u_t.emplace(get_self(), [&](auto& t) {
            t.source = source;
            t.total_cpu_us = total_cpu_us;
            t.total_net_words = total_net_words;
            t.submission_hash_list.push_back(hash);
        });

        _resource_config_state.submitting_oracles.push_back(source);


        // distribute inflation
        std::map<checksum256, uint8_t> hash_count;
        auto oracles = _resource_config_state.submitting_oracles;
        if (oracles.size() >= _resource_config_state.oracle_consensus_threshold) {

            // count number of each hash
            system_usage_table u_t(get_self(), get_self().value);
            for (int i=0; i<oracles.size(); i++) {
                auto ut_itr = u_t.begin();
                ut_itr = u_t.find(oracles[i].value);
                if (ut_itr->submission_hash_list.size() >= 1) {
                    hash_count[ut_itr->submission_hash_list[0]]++;
                }
            }

            // establish modal hash
            checksum256 modal_hash;
            uint8_t mode_count = 0;
            for (auto const& x : hash_count) {
                if (x.second > mode_count) {
                    modal_hash = x.first;
                    mode_count = x.second;
                }
            }

            // find dataset corresponding to modal hash, and distribute inflation
            if (mode_count >= _resource_config_state.oracle_consensus_threshold) {
                std::vector<metric> accounts_usage_data;
                datasets_table d_t(get_self(), get_self().value);
                auto dt_hash_index = d_t.get_index<"hash"_n>();
                auto dt_itr = dt_hash_index.find(modal_hash);
                if (dt_itr->hash == modal_hash) {
                    auto cpu_usage_us = dt_itr->data[0].u;
                    auto net_usage_words = dt_itr->data[1].u;

                    print(cpu_usage_us, " ", net_usage_words, "\n");

                    settotal(cpu_usage_us, net_usage_words, timestamp);


                }

            }

        }

    }

    // adds the CPU used by the accounts included (for calling oracle)
    // called after the oracle has set the total
    ACTION system_contract::addactusg(name source, uint16_t dataset_id, const std::vector<metric>& data, time_point_sec timestamp)
    {  
        require_auth(source);
//        check(is_source(source) == true, "not authorized to execute this action");

        int length = data.size();
        check(length>0, "must supply more than zero data values");
        check(length<=_resource_config_state.dataset_max_size, "must supply fewer data values");

        check(_resource_config_state.period_start == timestamp, "timestamp does not match period_start");

        system_usage_table u_t(get_self(), get_self().value);
        auto ut_itr = u_t.find(source.value);
        check(ut_itr != u_t.end(), "usage totals not set");
        check(dataset_id == ut_itr->submission_hash_list.size(), "dataset_id differs from expected value");

        std::string datatext = "";
        for (int i=0; i<length; i++) {
            auto account = data[i].a;
            auto cpu_usage_us = data[i].u;
            check(cpu_usage_us > 0, "account cpu measurement must be greater than 0");

            // add cpu to allocated amount for oracle to ensure not exceeding declared total
            auto unallocated_cpu = ut_itr->total_cpu_us - ut_itr->allocated_cpu;
            check(unallocated_cpu >= cpu_usage_us, "insufficient unallocated cpu");
            u_t.modify(ut_itr, get_self(), [&](auto& t) {
                t.allocated_cpu += cpu_usage_us;
            });

            // append usage pair to string for hashing
            datatext += account.to_string();
            datatext += std::to_string(cpu_usage_us);
        }

        // hash submitted data
        checksum256 hash = sha256(datatext.c_str(), datatext.size());
        u_t.modify(ut_itr, get_self(), [&](auto& t) {
            t.submission_hash_list.push_back(hash);
        });

        // add data and hash to table if not already present
        datasets_table d_t(get_self(), get_self().value);
        auto dt_hash_index = d_t.get_index<"hash"_n>();
        auto dt_itr = dt_hash_index.find(hash);
        if (dt_itr->hash != hash) {
            d_t.emplace(get_self(), [&](auto& t) {
                t.id = d_t.available_primary_key();
                t.hash = hash;
                t.data = data;
            });
        }

        // distribute user account rewards
        std::map<checksum256, uint8_t> hash_count;
        auto oracles = _resource_config_state.submitting_oracles;
        if (oracles.size() >= _resource_config_state.oracle_consensus_threshold) {

            // count number of each hash
            system_usage_table u_t(get_self(), get_self().value);
            for (int i=0; i<oracles.size(); i++) {
                auto ut_itr = u_t.begin();
                ut_itr = u_t.find(oracles[i].value);
                if (ut_itr->submission_hash_list.size() >= (dataset_id+1)) {
                    hash_count[ut_itr->submission_hash_list[dataset_id]]++;
                }
            }

            // establish modal hash
            checksum256 modal_hash;
            uint8_t mode_count = 0;
            for (auto const& x : hash_count) {
                if (x.second > mode_count) {
                    modal_hash = x.first;
                    mode_count = x.second;
                }
            }

            // find dataset corresponding to modal hash, and distribute inflation
            if (mode_count >= _resource_config_state.oracle_consensus_threshold) {
                std::vector<metric> accounts_usage_data;
                datasets_table d_t(get_self(), get_self().value);
                auto dt_hash_index = d_t.get_index<"hash"_n>();
                auto dt_itr = dt_hash_index.find(modal_hash);
                if (dt_itr->hash == modal_hash) {
                    accounts_usage_data = dt_itr->data;
                }

                // expensive part (100 accounts in ~9000us)
                auto core_sym = core_symbol();
                account_pay_table ap_t(get_self(), get_self().value);
                for (int i=0; i<accounts_usage_data.size(); i++) {
                    auto account = accounts_usage_data[i].a;
                    auto account_cpu = accounts_usage_data[i].u;
                    auto ap_itr = ap_t.find(account.value);
                    if (ap_itr == ap_t.end()) {
                        ap_t.emplace(get_self(), [&](auto& t) {
                            t.account = account;
                            t.payout = asset(account_cpu, core_sym);
                            t.timestamp = timestamp;
                        });
                    } else {
                        ap_t.modify(ap_itr, get_self(), [&](auto& t) {
                            t.payout += asset(account_cpu, core_sym);
                            t.timestamp = timestamp;
                        });
                    }
                    update_votes(account, 100); // ignores non existant accounts
                }

            }

        }

    }

    // called by individual accounts to claim their distribution
    ACTION system_contract::claimdistrib(name account)
    {
        require_auth(account);
//        check(!_resource_config_state.locked, "cannot claim while inflation calculation is running");

        account_pay_table a_t(get_self(), get_self().value);
        auto itr = a_t.find(account.value);
        check(itr != a_t.end(), "account not found");
        check(itr->payout != asset( 0, core_symbol() ), "zero balance to claim");

        auto feature_itr = _features.find("resource"_n.value);
        bool resource_active = feature_itr == _features.end() ? false : feature_itr->active;
        if(resource_active)
        {
            token::transfer_action transfer_act{token_account, {{usage_account, active_permission}, {account, active_permission}}};
            transfer_act.send(usage_account, account, itr->payout, "utility reward");
        }
        itr = a_t.erase(itr);
    }

    ACTION system_contract::addupdsource(name account, uint8_t in_out)
    {
        require_auth(get_self());
        sources_table s_t(get_self(), get_self().value);
        if (in_out == 0)
        {
            auto itr = s_t.find(account.value);
            check(itr != s_t.end(), "authorized source account not found during removal");
            itr = s_t.erase(itr);
        }
        else
        {
            auto itr2 = s_t.find(account.value);
            check(itr2 == s_t.end(), "authorized source account already exists in sourceauths table");
            s_t.emplace(get_self(), [&](auto &s) {
            s.account = account;
            });
        }
    }

    ACTION system_contract::activatefeat(name feature) {
        require_auth(get_self());
        feature_toggle_table f_t(get_self(), get_self().value);

        auto itr = f_t.find(feature.value);
        check(itr == f_t.end(), "feature already active");

        f_t.emplace(get_self(), [&](auto &f) {
            f.feature = feature;
            f.active = true;
        });
    }

    bool system_contract::is_source(name source)
    {
        sources_table s_t(get_self(), get_self().value);
        auto itr = s_t.find(source.value);
        if (itr == s_t.end())
        {
            return false;
        }
        else
        {
            return true;
        }
    }

   void system_contract::update_votes( const name& voter_name, uint64_t weight ) {
       auto itr = _voters.find(voter_name.value);
       if( itr == _voters.end() ) {
        return;
       }
        _voters.modify(itr, same_payer, [&](auto &v) {
                v.last_vote_weight += weight;
            });

       for( const auto &p: itr->producers ) {
           auto pitr = _producers.find(p.value);
           if(pitr == _producers.end()) {
               continue;
           }
            _producers.modify(pitr, same_payer, [&](auto &p) {
                p.total_votes += weight;
            });
       }
   }

}