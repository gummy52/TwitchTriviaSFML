// Steven Rogers

#include "stdafx.h"

TwitchIRC::TwitchIRC(Game* game, const std::string nick, const std::string usr, const std::string addr, const uint32 port, const std::string channel, const std::string password) :
	m_channelName(channel),
	m_sSocket(NULL),
	m_trivia(this),
	m_pGame(game)
{
	if (int32 err = WSAStartup(MAKEWORD(2, 0), &m_wsa))
	{
		printf("WSAStartup failed with error: %d\n", err);
	}
	else
	{
		printf("TwitchIRC: Constructed.\n");

		m_sSocket = Util::createSocketAndConnect(addr.c_str(), port, false, 1);

		if (m_sSocket == INVALID_SOCKET)
		{
			printf("Failed to connect to %s.\n", addr.c_str());
			return;
		}
		
		const std::string passSend = std::string("PASS " + password + "\r\n");
		const std::string nickSend = std::string("NICK " + nick + "\r\n");
		const std::string userSend = std::string("USER " + usr + " 0 * :" + usr + "\r\n");

		// The PASS command is used to set a 'connection password'.
		// The optional password can and MUST be set before any attempt to register the connection is made. 
		if (password.size())
			Util::sendBytes(m_sSocket, (char*)passSend.c_str(), (int32)passSend.size());

        Util::sendBytes(m_sSocket, (char*)nickSend.c_str(), (int32)nickSend.size());
		Util::sendBytes(m_sSocket, (char*)userSend.c_str(), (int32)userSend.size());
		
		// Wait for a response.
		std::string response;
		ReceiveIRCMessage(response);

		if (response.find("Welcome") == std::string::npos)
			endSocket();
		else
		{
			const std::string joinMsg = "JOIN " + m_channelName + "\r\n";
			
			if (Util::sendBytes(m_sSocket, (char*)joinMsg.c_str(), (int32)joinMsg.size()) != SOCKET_ERROR)
			{
				SendChatMsg("It's " + Util::timeStampToHReadble() + " and it's timeeeeee for some time trivvaaaa!!!");
				m_trivia.queueNextQuestion();
			}
		}
	}
} 

TwitchIRC::~TwitchIRC()
{
	if (m_sSocket)
		closesocket(m_sSocket);

	WSACleanup();
}

void TwitchIRC::Update()
{		
	std::string response;
	ReceiveIRCMessage(response);

	if (response.size())
	{
		// If we saw someone write a private message
		if (response.find("PRIVMSG") != std::string::npos)
		{
			std::string username;
			std::string message;
			StripPRIVMSG(response, username, message);
			printf("PRIVMSG %s: %s\n", username.c_str(), message.c_str());
			m_trivia.ProcessAnswer(username, message);
		}

		// If server wants a pong
		else if (response.find("PING") != std::string::npos) 
		{
			SendPong(response);
		}
	};

	m_trivia.Update();
}

void TwitchIRC::StripPRIVMSG(const std::string ircMsg, std::string& username, std::string& msg)
{
	size_t nameEnds = ircMsg.find("!");
	size_t questionBegins = ircMsg.find(m_channelName + " :") + m_channelName.size() + 2;
	
	// Check its format as :NAME!
	if (ircMsg[0] == ':' && nameEnds != std::string::npos)
	{
		for (size_t i = 1; i < nameEnds; ++i)
			username.push_back(ircMsg[i]);
	}
	
	if (questionBegins != std::string::npos)
	{
		for (size_t i = questionBegins; i < ircMsg.size() - 2; ++i)
			msg.push_back(ircMsg[i]);
	}
}

void TwitchIRC::SendPong(const std::string incomingMsg)
{
	const std::string msg = "PONG " + m_channelName + "\r\n";

	if (Util::sendBytes(m_sSocket, (char*)msg.c_str(), (int32)msg.size()) == SOCKET_ERROR)
		endSocket();

	printf("Replied to a ping.\n");
}

bool TwitchIRC::ReceiveIRCMessage(std::string& message)
{
	// IRC doesn't tell us how big the incoming packet is, we just grab data until we see a "\r\n"
	while (activeSocket())
	{
		char letter;
		int32 result = recv(m_sSocket, &letter, 1, 0);

		if (result == SOCKET_ERROR)
		{
			int32 error_code = 0;
			int32 error_code_size = sizeof(error_code);
			getsockopt(m_sSocket, SOL_SOCKET, SO_ERROR, (char*)&error_code, &error_code_size);

			// Means that recv timed out but there was no error, which is fine.
			if (!error_code)
				return true;
			else
			{
				endSocket();
				return false;
			}
		}
		else
			message.push_back(letter);

		if (message.size() > 1 && message[message.size() - 2] == '\r' &&message[message.size() - 1] == '\n')
			return true;

		// Would never get this big. If this has happened, shut it down.
		if (message.size() > MAX16BIT)
			endSocket();
	}

	return false;
}

bool TwitchIRC::SendChatMsg(std::string chatMsg)
{	
	if (!activeSocket())
		return false;

	printf("Sending: '%s'\n", chatMsg.c_str());

	m_pGame->SetDrawText(chatMsg);

	if (chatMsg.find("Q: ") != std::string::npos)
		m_pGame->PlaySound("sfx\\new_question.ogg");

	if (chatMsg.find("Hurry, time will") != std::string::npos)
		m_pGame->PlaySound("sfx\\time_almost_up.ogg");

	if (chatMsg.find("Time has expired") != std::string::npos)
		m_pGame->PlaySound("sfx\\time_up.ogg");

	if (chatMsg.find("He has done it!") != std::string::npos)
		m_pGame->OnRewardedPoints(m_trivia.getCurrentQuestion().points);

	const std::string formattedMsg = "PRIVMSG " + m_channelName + " :" + chatMsg + "\r\n";
	
	if (Util::sendBytes(m_sSocket, (char*)formattedMsg.c_str(), (int32)formattedMsg.size()) == SOCKET_ERROR)
	{
		endSocket();
		return false;
	}

	return true;
}