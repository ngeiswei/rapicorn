// This Source Code Form is licensed MPLv2: http://mozilla.org/MPL/2.0
#include <stdio.h>
#include "components-a1-server.hh"
#include "components-a1-client.hh"

using namespace Rapicorn;
using namespace Rapicorn::Aida;


class MiniServerImpl;
typedef std::shared_ptr<MiniServerImpl> MiniServerImplP;

class MiniServerImpl : public A1::MiniServerIface {
  MainLoopP         loop_;
  ServerConnectionP connection_;
  class LoopSource : public EventSource {
    ServerConnection &connection_;
    PollFD pfd_;
  public:
    LoopSource (ServerConnection &connection) :
      connection_ (connection)
    {
      pfd_.fd = connection_.notify_fd();
      pfd_.events = PollFD::IN;
      pfd_.revents = 0;
      add_poll (&pfd_);
    }
    virtual bool prepare  (const LoopState &state, int64 *timeout_usecs_p) override { return connection_.pending(); }
    virtual bool check    (const LoopState &state) override                         { return connection_.pending(); }
    virtual bool dispatch (const LoopState &state) override                         { connection_.dispatch(); return true; }
  };
  bool          vbool_;
  int32         vi32_;
  int64         vi64t_;
  double        vf64_;
  String        vstr_;
  A1::CountEnum count_;
public:
  virtual bool          vbool () const          override { return vbool_; }
  virtual void          vbool (bool b)          override { vbool_ = b; }
  virtual int32         vi32  () const          override { return vi32_; }
  virtual void          vi32  (int32 i)         override { vi32_ = i; }
  virtual int64         vi64t () const          override { return vi64t_; }
  virtual void          vi64t (int64 i)         override { vi64t_ = i; }
  virtual double        vf64  () const          override { return vf64_; }
  virtual void          vf64  (double f)        override { vf64_ = f; }
  virtual String        vstr  () const          override { return vstr_; }
  virtual void          vstr  (const String &s) override { vstr_ = s; }
  virtual A1::CountEnum count () const          override { return count_; }
  virtual void          count (A1::CountEnum v) override { count_ = v; }
  MiniServerImpl () : loop_ (MainLoop::create()), vbool_ (false) {}
  bool
  bind (const String &address)
  {
    assert_return (connection_ == NULL, false);
    connection_ = ServerConnection::bind<MiniServerIface> (address, shared_ptr_cast<MiniServerIface> (this));
    if (connection_)
      loop_->add (std::make_shared<LoopSource> (*connection_.get()));
    return connection_ != NULL;
  }
  EventLoop&   loop    ()                            { return *loop_; }
  int          run     ()                            { return loop_->run(); }
  virtual void message (const String &what) override { printout ("%s\n", what); }
  virtual void quit    ()                            { loop_->quit(); } // FIXME: loop is quit before remote references can be cleared
};

static void
a1_server_thread (AsyncBlockingQueue<String> *notify_queue)
{
  MiniServerImplP mini_server = std::make_shared<MiniServerImpl> ();
  const bool success = mini_server->bind ("inproc://aida-test-mini-server");
  if (!success)
    {
      notify_queue->push (string_format ("%s: failed to start mini-server: %s", __func__, strerror (errno)));
      return;
    }
  mini_server->loop().exec_now ([notify_queue] () { notify_queue->push ("OK"); });
  notify_queue = NULL; // remote destroys notify_queue after receiving our message
  mini_server->run();
}

class AccessorVisitor {
  std::vector<Parameter> &parameters_;
public:
  explicit AccessorVisitor (std::vector<Parameter> &parameters) :
    parameters_ (parameters)
  {}
  template<class Klass, typename SetterType, typename GetterType> void
  operator() (Klass &instance, const char *field_name, void (Klass::*setter) (SetterType), GetterType (Klass::*getter) () const)
  {
    parameters_.push_back (Parameter (instance, field_name, setter, getter));
  }
};

static Parameter*
parameter_vector_find (std::vector<Parameter> &params, const String &name)
{
  for (size_t i = 0; i < params.size(); i++)
    if (params[i].field_name() == name)
      return &params[i];
  return NULL;
}

static void
test_server (A1::MiniServerH server)
{
  Any a;
  // create Parameter for the server properties
  std::vector<Parameter> params;
  AccessorVisitor av (params);
  server.__accept_accessor__ (av);
  assert (params.size() > 0);
  // check known properties
  Parameter *param_vbool = parameter_vector_find (params, "vbool");
  assert (param_vbool != NULL);
  Parameter *param_vi32 = parameter_vector_find (params, "vi32");
  assert (param_vi32 != NULL);
  Parameter *param_vi64t = parameter_vector_find (params, "vi64t");
  assert (param_vi64t != NULL);
  Parameter *param_vf64 = parameter_vector_find (params, "vf64");
  assert (param_vf64 != NULL);
  Parameter *param_vstr = parameter_vector_find (params, "vstr");
  assert (param_vstr != NULL);
  Parameter *param_count = parameter_vector_find (params, "count");
  assert (param_count != NULL);
  // vbool
  a = param_vbool->get();
  assert (a.kind() == Aida::BOOL);
  assert (a.get<bool>() == false);
  a.set (true);
  param_vbool->set (a);
  assert (server.vbool() == true);
  assert (param_vbool->get_aux ("blurb") == "Just true or false");
  assert (string_to_bool (param_vbool->get_aux ("default")) == true);
  assert (param_vbool->get_aux ("hints") == "rw");
  // vi32
  a = param_vi32->get();
  assert (a.kind() == Aida::INT64);
  assert (a.get<int32>() == 0);
  a.set (-750);
  param_vi32->set (a);
  Any b = param_vi32->get();
  assert (b.get<double>() == -750);
  assert (server.vi32() == -750);
  assert (param_vi32->get_aux ("label") == "Int32 Value");
  assert (string_to_int (param_vi32->get_aux ("min")) == -2147483648);
  assert (string_to_int (param_vi32->get_aux ("max")) == 2147483647);
  assert (string_to_int (param_vi32->get_aux ("step")) == 256);
  assert (string_to_int (param_vi32->get_aux ("default")) == 32768);
  assert (param_vi32->get_aux ("hints") == "rw");
  // vi64t
  a = param_vi64t->get();
  assert (a.kind() == Aida::INT64);
  assert (a.get<int32>() == 0);
  a.set (-750);
  param_vi64t->set (a);
  b = param_vi64t->get();
  assert (b.get<double>() == -750);
  assert (server.vi64t() == -750);
  assert (param_vi64t->get_aux ("label") == "Int64 Value");
  assert (string_to_int (param_vi64t->get_aux ("min")) == -9223372036854775808ULL);
  assert (string_to_int (param_vi64t->get_aux ("max")) == 9223372036854775807);
  assert (string_to_int (param_vi64t->get_aux ("step")) == 65536);
  assert (string_to_int (param_vi64t->get_aux ("default")) == -65536);
  assert (param_vi64t->get_aux ("hints") == "rw");
  // vf64
  a = param_vf64->get();
  assert (a.kind() == Aida::FLOAT64);
  assert (a.get<double>() == 0);
  a.set (-0.75);
  param_vf64->set (a);
  b = param_vf64->get();
  assert (b.get<double>() == -0.75);
  assert (server.vf64() == -0.75);
  assert (param_vf64->get_aux ("label") == "Float Value");
  assert (string_to_float (param_vf64->get_aux ("min")) == 0.0);
  assert (string_to_float (param_vf64->get_aux ("max")) == 1.0);
  assert (string_to_float (param_vf64->get_aux ("step")) == 0.1);
  assert (param_vf64->get_aux ("hints") == "rw");
  // vstr
  a = param_vstr->get();
  assert (a.kind() == Aida::STRING);
  assert (a.get<String>() == "");
  server.vstr ("123");
  a = param_vstr->get();
  assert (a.get<String>() == "123");
  a.set ("ZOOT");
  param_vstr->set (a);
  assert (server.vstr() == "ZOOT");
  assert (param_vstr->get_aux ("default") == "foobar");
  assert (param_vstr->get_aux ("hints") == "rw");
  // count
  server.count (A1::TWO);
  a = param_count->get();
  assert (a.kind() == Aida::ENUM);
  assert (a.get<A1::CountEnum>() == A1::TWO);
  a.set (A1::THREE);
  assert (a.kind() == Aida::ENUM);
  param_count->set (a);
  assert (server.count() == A1::THREE);
  assert (param_count->get_aux ("hints") == "rw");
  assert (string_to_int (param_count->get_aux ("default")) == 2);
  // echo
  server.message ("  CHECK  MiniServer property access                                      OK");
}

static void
test_a1_server ()
{
  AsyncBlockingQueue<String> notify_queue;
  std::thread sthread (a1_server_thread, &notify_queue);
  String result = notify_queue.pop();
  if (result == "OK")
    {
      ClientConnectionP connection = ClientConnection::connect ("inproc://aida-test-mini-server");
      A1::MiniServerH server;
      if (connection)
        server = connection->remote_origin<A1::MiniServerH>();
      if (!connection)
        result = string_format ("%s: failed to connect to mini-server: %s", __func__, strerror (errno));
      if (!server)
        result = string_format ("%s: failed to talk to mini-server: %s", __func__, strerror (errno));
      else
        {
          server.message ("  CHECK  MiniServer remote call successfull                              OK");
          test_server (server);
          server.quit();
        }
      new ClientConnectionP (connection); // FIXME: need to leak this because ~ClientConnection is unsupported
    }
  if (result != "OK")
    fatal ("%s: %s", __func__, result);
  sthread.join();
}
