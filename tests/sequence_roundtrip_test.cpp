#include <gtest/gtest.h>

#include <google/protobuf/util/json_util.h>

#include "sequence.pb.h"

TEST(SequenceProto, RoundTripBinary) {
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("notes");
  auto* step = seq.add_steps();
  step->set_method("/notes.Notes/Echo");
  step->set_request_type("notes.EchoRequest");
  step->set_principal_id("alice");
  step->set_request("raw");

  auto* binding = seq.add_bindings();
  binding->set_from_step(0);
  binding->mutable_from_field()->add_parts("note_id");
  binding->set_to_step(1);
  binding->mutable_to_field()->add_parts("note_id");

  std::string bytes;
  ASSERT_TRUE(seq.SerializeToString(&bytes));

  grpc_authz_fuzz::Sequence parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.target_id(), "notes");
  ASSERT_EQ(parsed.steps_size(), 1);
  EXPECT_EQ(parsed.steps(0).method(), "/notes.Notes/Echo");
  EXPECT_EQ(parsed.steps(0).request_type(), "notes.EchoRequest");
  ASSERT_EQ(parsed.bindings_size(), 1);
  EXPECT_EQ(parsed.bindings(0).from_field().parts(0), "note_id");
}

TEST(SequenceProto, JsonRoundTrip) {
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("notes");
  auto* step = seq.add_steps();
  step->set_method("/notes.Notes/PutNote");
  step->set_request_type("notes.PutNoteRequest");
  step->set_principal_id("bob");

  std::string json;
  auto status = google::protobuf::util::MessageToJsonString(seq, &json);
  ASSERT_TRUE(status.ok()) << status.message();

  grpc_authz_fuzz::Sequence parsed;
  status = google::protobuf::util::JsonStringToMessage(json, &parsed);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(parsed.target_id(), "notes");
  ASSERT_EQ(parsed.steps_size(), 1);
  EXPECT_EQ(parsed.steps(0).principal_id(), "bob");
}
