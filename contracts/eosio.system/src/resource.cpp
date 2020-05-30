#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>


namespace eosiosystem {

    ACTION system_contract::initresource(uint16_t dataset_max_size, uint16_t oracle_consensus_threshold, time_point_sec period_start)
    {
        require_auth(get_self());

        _resource_config_state.dataset_max_size = dataset_max_size;
        _resource_config_state.oracle_consensus_threshold = oracle_consensus_threshold;
        _resource_config_state.period_start = period_start;
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

        system_usage_table u_t(get_self(), get_self().value);
        auto itr = u_t.find(source.value);

        check(itr == u_t.end(), "total already set");

        // hash submitted data
        std::string datatext = std::to_string(total_cpu_us) + std::to_string(total_net_words);
        checksum256 hash = sha256(datatext.c_str(), datatext.size());

        // add totals data
        u_t.emplace(get_self(), [&](auto& t) {
            t.source = source;
            t.total_cpu_us = total_cpu_us;
            t.total_net_words = total_net_words;
            t.submission_hash_list.push_back(hash);
        });

        _resource_config_state.submitting_oracles.push_back(source);
    }

    // adds the CPU used by the accounts included (for calling oracle)
    // called after the oracle has set the total
    ACTION system_contract::addactusg(name source, uint16_t dataset_id, const std::vector<account_cpu>& data, time_point_sec timestamp)
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
                if (ut_itr->submission_hash_list.size() >= dataset_id) {
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
                std::vector<account_cpu> accounts_usage_data;
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
                }

            }

        }

    }

    // called by individual accounts to claim their distribution
    ACTION system_contract::claimdistrib(name account)
    {
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