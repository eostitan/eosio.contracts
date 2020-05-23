#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>


namespace eosiosystem {

    // sets total resources used by system (for calling oracle)
    // this must be called by oracle before adding individual cpu usage
    ACTION system_contract::settotalusg(name source, uint64_t total_cpu_us, uint64_t total_net_words, time_point_sec timestamp)
    {
        require_auth(source);
//        check(is_source(source) == true, "not authorized to execute this action");

        check(total_cpu_us > 0, "cpu measurement must be greater than 0");
        check(total_net_words > 0, "net measurement must be greater than 0");

        check(_resource_config_state.period_start == timestamp, "timestamp does not match period_start");

        system_usage_table u_t(get_self(), get_self().value);
        auto itr = u_t.find(source.value);

        check(itr == u_t.end(), "total already set");

        u_t.emplace(get_self(), [&](auto& t) {
            t.source = source;
            t.total_cpu_us = total_cpu_us;
            t.total_net_words = total_net_words;
        });
    }

    // adds the CPU used by the accounts included (for calling oracle)
    // called after the oracle has set the total
    ACTION system_contract::addactusg(name source, const std::vector<account_cpu>& data, time_point_sec timestamp)
    {  
        require_auth(source);
//        check(is_source(source) == true, "not authorized to execute this action");

        int length = data.size();
        check(length>0, "must supply more than zero data values");
        check(length<100, "must supply less than 100 data values");

        check(_resource_config_state.period_start == timestamp, "timestamp does not match period_start");

        system_usage_table u_t(get_self(), get_self().value);
        auto ut_itr = u_t.find(source.value);
        check(ut_itr != u_t.end(), "usage totals not set");
        check(!ut_itr->data_committed, "data already committed");

        for (int i=0; i<length; i++) {
            auto account = data[i].account;
            auto cpu_usage_us = data[i].cpu_usage_us;
            check(cpu_usage_us > 0, "account cpu measurement must be greater than 0");

            // add usage record
            account_usage_table au_t(get_self(), source.value);
            auto itr = au_t.find(data[i].account.value);
            check(itr == au_t.end(), "an account cpu record was duplicated");
            au_t.emplace(source, [&](auto& t) {
                t.account = account;
                t.total_cpu_us = cpu_usage_us;
            });

            // add cpu to allocated amount for oracle to ensure not exceeding declared total
            auto unallocated_cpu = ut_itr->total_cpu_us - ut_itr->allocated_cpu;
            check(unallocated_cpu >= cpu_usage_us, "insufficient unallocated cpu");
            u_t.modify(ut_itr, get_self(), [&](auto& t) {
                t.allocated_cpu += cpu_usage_us;
            });
        }
    }

    // called by each oracle after they've sent all their data
    ACTION system_contract::commitusage(name source, time_point_sec timestamp) {
        require_auth(source);
//        check(is_source(source) == true, "not authorized to execute this action");

        check(_resource_config_state.period_start == timestamp, "timestamp does not match period_start");

        system_usage_table u_t(get_self(), get_self().value);
        auto ut_itr = u_t.find(source.value);
        check(ut_itr != u_t.end(), "usage totals not set");
        check(!ut_itr->data_committed, "data already committed");

        u_t.modify(ut_itr, get_self(), [&](auto& t) {
            t.data_committed = true;
        });
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

    ACTION system_contract::initresource(time_point_sec start)
    {
        require_auth(get_self());

        // TODO - prevent this if it has already been called (after testing)

        _resource_config_state.period_start = start;
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