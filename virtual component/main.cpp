#include <mqtt/async_client.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;

/* ---- Fixed config ---- */
static const string ADDRESS  = "tcp://mqtt-dev.precise.seas.upenn.edu:1883";
static const string USERNAME = "cis441-541_2025";
static const string PASSWORD = "cukwy2-geNwit-puqced";
static const int    QOS = 1;

static const string TEAM  = "BestAPS";
static const string PFX   = "cis441-541/" + TEAM;

/* Topic mapping (Part 1 with validator) */
static const string TOPIC1_VAL_TO_PUMP = PFX + "/insulin-pump-openaps"; // validator → pump
static const string TOPIC2_PUMP_TO_VP  = PFX + "/insulin-pump";         // pump → VP
static const string TOPIC3_CGM_TO_VAL  = PFX + "/cgm-openaps";          // CGM → validator
static const string TOPIC4_VP_TO_CGM   = PFX + "/cgm";                   // VP → CGM

class Callback : public virtual mqtt::callback {
    mqtt::async_client& cli_;
public:
    explicit Callback(mqtt::async_client& c) : cli_(c) {}

    void connected(const string&) override {
        cout << "[vc] connected(cb). subscribing..." << endl;
        cli_.subscribe(TOPIC1_VAL_TO_PUMP, QOS);
        cli_.subscribe(TOPIC4_VP_TO_CGM, QOS);
        cout << "[vc] subscribed(cb): " << TOPIC1_VAL_TO_PUMP << " , " << TOPIC4_VP_TO_CGM << endl;
    }

    void connection_lost(const string& cause) override {
        cerr << "[vc] connection lost: " << cause << endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        const string t = msg->get_topic();
        const string p = msg->to_string();
        try {
            if (t == TOPIC1_VAL_TO_PUMP) {
                cli_.publish(TOPIC2_PUMP_TO_VP, p, QOS, false);
                cout << "[vc] BRIDGE INS  1->2 : " << t << " -> " << TOPIC2_PUMP_TO_VP << endl;
            } else if (t == TOPIC4_VP_TO_CGM) {
                cli_.publish(TOPIC3_CGM_TO_VAL, p, QOS, false);
                cout << "[vc] BRIDGE CGM  4->3 : " << t << " -> " << TOPIC3_CGM_TO_VAL << endl;
            } else {
                cout << "[vc] IGNORE: " << t << endl;
            }
        } catch (const std::exception& e) {
            cerr << "[vc] publish error: " << e.what() << endl;
        }
    }
};

class App {
    mqtt::async_client cli_;
    Callback cb_;
public:
    App() : cli_(ADDRESS, "vc-part1-" + TEAM), cb_(cli_) {
        cout << "[vc] CONFIG\n"
             << "  ADDRESS=" << ADDRESS << "\n"
             << "  USERNAME=" << USERNAME << "\n"
             << "  TEAM=" << TEAM << "\n"
             << "  T1 (val→pump)=" << TOPIC1_VAL_TO_PUMP << "\n"
             << "  T2 (pump→vp) =" << TOPIC2_PUMP_TO_VP  << "\n"
             << "  T3 (cgm→val) =" << TOPIC3_CGM_TO_VAL  << "\n"
             << "  T4 (vp→cgm)  =" << TOPIC4_VP_TO_CGM   << endl;

        cli_.set_callback(cb_);
        mqtt::connect_options conn;
        conn.set_user_name(USERNAME);
        conn.set_password(PASSWORD);
        conn.set_clean_session(true);
        conn.set_automatic_reconnect(true);

        cout << "[vc] connecting..." << endl;
        cli_.connect(conn)->wait();
        cout << "[vc] connected." << endl;

        // immediate subscribe on first connect
        cli_.subscribe(TOPIC1_VAL_TO_PUMP, QOS);
        cli_.subscribe(TOPIC4_VP_TO_CGM, QOS);
        cout << "[vc] subscribed(init): " << TOPIC1_VAL_TO_PUMP << " , " << TOPIC4_VP_TO_CGM << endl;
    }

    void run() {
        cout << "[vc] running. Press Ctrl+C to exit." << endl;
        while (true) this_thread::sleep_for(seconds(1));
    }
};

int main() {
    try { App().run(); }
    catch (const std::exception& e) {
        cerr << "[vc] fatal: " << e.what() << endl; return 1;
    }
    return 0;
}
