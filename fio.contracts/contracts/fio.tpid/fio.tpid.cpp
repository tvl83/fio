/** FioTPID implementation file
 *  Description:
 *  @author Adam Androulidakis
 *  @modifedby
 *  @file fio.tpid.cpp
 *  @copyright Dapix
 */

#include "fio.tpid.hpp"

namespace fioio {

    class [[eosio::contract("TPIDController")]]  TPIDController : public eosio::contract {

    private:
        tpids_table tpids;
        fionames_table fionames;
        eosiosystem::voters_table voters;
        bounties_table bounties;

    public:
        using contract::contract;

        TPIDController(name s, name code, datastream<const char *> ds) :
                contract(s, code, ds), tpids(_self, _self.value), bounties(_self, _self.value),
                fionames(SystemContract, SystemContract.value),
                voters(SystemContract, SystemContract.value) {
        }

        // this action will perform the logic of checking the voter_info,
        // and setting the proxy and auto proxy for auto proxy.
        inline void autoproxy(name proxy_name, name owner_name) {

            //check the voter_info table for a record matching owner_name.
            auto viter = voters.find(owner_name.value);
            if (viter == voters.end()) {
                //if the record is not there then send inline action to crautoprx (a new action in the system contract).
                //note this action will set the auto proxy and is_aut_proxy, so return after.
                INLINE_ACTION_SENDER(eosiosystem::system_contract, crautoproxy)(
                        "eosio"_n, {{get_self(), "active"_n}},
                        {proxy_name, owner_name});
                return;
            } else {
                //check if the record has auto proxy and proxy matching proxy_name, set has_proxy. if so return.
                if (viter->is_auto_proxy) {
                    if (proxy_name == viter->proxy) {
                        return;
                    }
                } else if ((viter->proxy) || (viter->producers.size() > 0) || viter->is_proxy) {
                    //check if the record has another proxy or producers. if so return.
                    return;
                }

                //invoke the fio.system contract action to set auto proxy and proxy name.
                action(
                        permission_level{get_self(), "active"_n},
                        "eosio"_n,
                        "setautoproxy"_n,
                        std::make_tuple(proxy_name, owner_name)
                ).send();
            }
        }

        inline void process_auto_proxy(const string &tpid, name owner) {
            uint128_t hashname = string_to_uint128_hash(tpid.c_str());
            auto tpidsbyname = tpids.get_index<"byname"_n>();
            auto iter = tpidsbyname.find(hashname);
            if (iter != tpidsbyname.end()) {
                //tpid exists, use the info to find the owner of the tpid
                auto namesbyname = fionames.get_index<"byname"_n>();
                auto iternm = namesbyname.find(iter->fioaddhash);
                if (iternm != namesbyname.end()) {
                    name proxy_name = name(iternm->owner_account);
                    //do the auto proxy
                    autoproxy(proxy_name, owner);
                }
            }
        }

        //Condition: check if tpid exists in fionames before executing.
        //This call should only be made by processrewards and processbucketrewards in fio.rewards.hpp
        //which should also only be called by contracts that collect fees
        //@abi action
        [[eosio::action]]
        void updatetpid(const string &tpid, const name owner, const uint64_t &amount) {

            eosio_assert(has_auth(SystemContract) || has_auth(TokenContract) || has_auth(TREASURYACCOUNT) ||
                         has_auth("fio.reqobt"_n) || has_auth("eosio"_n),
                         "missing required authority of fio.system, fio.treasury, fio.token, eosio or fio.reqobt");

            uint128_t tpidhash = string_to_uint128_hash(tpid.c_str());
            auto tpidsbyname = tpids.get_index<"byname"_n>();
            if (tpidsbyname.find(tpidhash) == tpidsbyname.end()) {

                uint64_t id = tpids.available_primary_key();
                tpids.emplace(get_self(), [&](struct tpid &f) {
                    f.id = id;
                    f.fioaddress = tpid;
                    f.fioaddhash = tpidhash;
                    f.rewards = 0;
                });

                process_auto_proxy(tpid, owner);
            }
            //Update existing tpid amount or amount of tpid that was just created before
            tpidsbyname.modify(tpidsbyname.find(tpidhash), get_self(), [&](struct tpid &f) {
                f.rewards += amount;
            });

        } //updatetpid

        //This action can only be called by fio.treasury after successful rewards payment to tpid
        //@abi action
        [[eosio::action]]
        void rewardspaid(const string &tpid) {
            require_auth(TREASURYACCOUNT);
            uint128_t fioaddhash = string_to_uint128_hash(tpid.c_str());

            auto tpidsbyname = tpids.get_index<"byname"_n>();
            auto tpidfound = tpidsbyname.find(fioaddhash);

            if (tpidfound != tpidsbyname.end()) {
                tpidsbyname.modify(tpidfound, _self, [&](struct tpid &f) {
                    f.rewards = 0;
                });
            }
        }

        //Must be called at least once at genesis for tokensminted check in fio.rewards.hpp
        //@abi action
        [[eosio::action]]
        void updatebounty(const uint64_t &amount) {
            eosio_assert((has_auth(TPIDContract) || has_auth(TREASURYACCOUNT)),
                         "missing required authority of fio.tpid, or fio.treasury");

            if (!bounties.exists()) {
                bounties.set(bounty{amount}, _self);
            } else {
                bounties.set(bounty{bounties.get().tokensminted + amount}, _self);
            }
        }
    }; //class TPIDController

    EOSIO_DISPATCH(TPIDController, (updatetpid)(rewardspaid)
    (updatebounty))
}
