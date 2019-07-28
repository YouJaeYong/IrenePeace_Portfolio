my_id = 99999;

function set_myid(x)
my_id = x;
end

function player_notify(player_id, x, y)
API_send_chat_packet(player_id, my_id, "Targeting  ");
end