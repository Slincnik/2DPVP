CREATE TABLE match_results (
    match_id UUID PRIMARY KEY,
    player_a_id UUID NOT NULL REFERENCES users(id),
    player_b_id UUID NOT NULL REFERENCES users(id),
    winner_player_id UUID NULL REFERENCES users(id),
    finish_reason VARCHAR(32) NOT NULL CHECK (finish_reason IN ('ko', 'time_limit', 'disconnect')),
    ended_at TIMESTAMPTZ NOT NULL,
    CHECK (player_a_id <> player_b_id),
    CHECK (winner_player_id IS NULL OR winner_player_id = player_a_id OR winner_player_id = player_b_id)
);

CREATE INDEX match_results_player_a_id_idx ON match_results(player_a_id);
CREATE INDEX match_results_player_b_id_idx ON match_results(player_b_id);
