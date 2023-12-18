#include <list>
#include <map>
#include <iostream>
#include <asio.hpp>
#include <regex>

////////////////////////////////////////////////////////////////////////////////
// Server //////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class Server
{
  private:
    typedef asio::ip::tcp::socket Socket;

    // Client vu du serveur (pointeurs intelligents).
    class Client : public std::enable_shared_from_this<Client>
    {
      private:
        Server * m_server;
        Socket m_socket;
        asio::streambuf m_buffer;
        std::string m_alias;
        bool m_active;

      public:
        Client (Server *, Socket &&);
        void start ();
        void stop ();
        inline std::string alias () const;
        void rename (const std::string &);
        void read ();
        void write (const std::string &);
    };

    // Pointeur intelligent.
    typedef std::shared_ptr<Client> ClientPtr;
    // Signature d'un processeur.
    typedef void (Server::*Processor) (ClientPtr, const std::string &);
    // Processeurs.
    static const std::map<std::string, Processor> PROCESSORS;

  private:
    asio::io_context m_context;
    asio::ip::tcp::acceptor m_acceptor;
    std::list<ClientPtr> m_clients;

  private:
    // Connexions entrantes.
    void accept ();
    // Recherche par alias.
    ClientPtr find (const std::string & alias);
    // Traitement d'une commande.
    void process (ClientPtr, const std::string &);
    // Traitement de la commande quit.
    void process_quit (ClientPtr client, const std::string &);
    // Traitement de la commande list.
    void process_list (ClientPtr client, const std::string & = "");
    // Traitement de la commande private.
    void process_private (ClientPtr client, const std::string & = "");
    // Traitement de la commande alias.
    void process_alias(ClientPtr client, const std::string &);
    // Processeurs.
    void process_message (ClientPtr, const std::string &);
    // Diffusion d'un message.
    void broadcast (const std::string & message, ClientPtr emitter = nullptr);
    // Suppression d'un client.
    void remove (ClientPtr);

  public:
    // Constructeur.
    Server (unsigned short port);
    // Démarrage.
    void start ();

  public:
    static const std::string INVALID_ALIAS;
    static const std::string INVALID_COMMAND;
    static const std::string INVALID_RECIPIENT;
    static const std::string MISSING_ARGUMENT;
};

////////////////////////////////////////////////////////////////////////////////
// Client //////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

Server::Client::Client (Server * server, Socket && socket) :
  m_server {server},
  m_socket {std::move (socket)},
  m_active {false}
{
  std::cout << "Nouveau client !" << std::endl;
}

void Server::Client::start ()
{
  if (m_active) return;

  // Pointeur intelligent pour assurer la survie de l'objet.
  ClientPtr self = shared_from_this ();

  // Lecture asynchrone.
  async_read_until (m_socket, m_buffer, '\n',
    [this, self] (const std::error_code & ec, std::size_t n) {
      // Erreur ?
      if (! ec) {
        std::istream is {&m_buffer};
        std::string alias;
        std::getline (is, alias, ' ');
        alias = std::regex_replace(alias, std::regex("\\s"), "");
        if(m_server->find(alias) == nullptr) {
          self->m_alias = alias;
          self->write("#alias " + alias);
          m_server->process_list(self);
          m_server->broadcast("#connected " + alias, self);
          self->m_active = true;
          self->read();
        }
        else
          self->write(Server::INVALID_ALIAS);
      }
      else
      {
        std::cout << "Bonjour, au revoir !" << std::endl;
        m_server->m_clients.remove (self);
      }
    });
}

void Server::Client::stop ()
{
  m_active = false;
}

std::string Server::Client::alias () const
{
  return m_alias;
}

void Server::Client::rename (const std::string & alias)
{
  m_alias = alias;
  write ("#alias " + alias);
}

void Server::Client::read ()
{
  //std::cout << "Client::read" << std::endl;

  // Pointeur intelligent pour assurer la survie de l'objet.
  ClientPtr self = shared_from_this ();

  // Lecture asynchrone.
  async_read_until (m_socket, m_buffer, '\n',
    [this, self] (const std::error_code & ec, std::size_t n) {
      // Erreur ?
      if (! ec) {
        std::istream is {&m_buffer};
        std::string message;
        // Traiter tous les messages disponibles.
        while (std::getline (is, message))
          m_server->process (self, message);
        // Si le client est toujours actif, lire à nouveau.
        if (m_active) read ();
      }
      else
      {
        std::cout << "Déconnexion intempestive !" << std::endl;
        m_server->broadcast("#disconnected " + self->alias(), self);
        m_server->m_clients.remove (self);
        m_server->process_quit (self, std::string {});
      }
    });
}

void Server::Client::write (const std::string & message)
{
  // Ajout du caractère "fin de ligne".
  std::string m = message + '\n';
  // Écriture asynchrone.
  async_write (m_socket,
               asio::buffer (m.data (), m.length ()),
               [this] (const std::error_code & ec, std::size_t n) {
                 // NE RIEN FAIRE
               });
}

////////////////////////////////////////////////////////////////////////////////
// Server //////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

Server::Server (unsigned short port) :
  m_context {},
  m_acceptor {m_context, asio::ip::tcp::endpoint {asio::ip::tcp::v4 (), port}},
  m_clients {}
{
}

void Server::start ()
{
  // Acceptation des connexions entrantes.
  accept ();

  // Démarrage du contexte.
  m_context.run ();
}

Server::ClientPtr Server::find (const std::string & alias)
{
  for (ClientPtr client: this->m_clients)
  {
    if(client->alias() == alias)
    {
      return client;
    }
  }

  return nullptr;
}

void Server::accept ()
{
  m_acceptor.async_accept (
    [this] (const std::error_code & ec, Socket && socket)
    {
      // Erreur ?
      if (! ec)
      {
        m_clients.emplace_back (std::make_shared<Client> (this, std::move (socket)));
        m_clients.back ()->start ();
      }

      accept();
    });
}

void Server::process (ClientPtr client, const std::string & message)
{
  // Lecture d'une éventuelle commande.
  std::istringstream iss (message);
  std::string command;
  if (iss >> command)
  {
    // Commande ?
    if (command[0] == '/')
    {
      // Consommation des caractères blancs.
      iss >> std::ws;
      // Reste du message.
      std::string data {std::istreambuf_iterator<char> {iss}, std::istreambuf_iterator<char> {}};

      // Recherche du processeur correspondant.
      // - S'il existe, l'appeler ;
      // - Sinon, "#invalid_command" !
      auto search = PROCESSORS.find(command);
      if(search != PROCESSORS.end())
      {
        Server::Processor proc = search->second;

        (this->*proc)(client, data);
      }
      else {
        client->write(Server::INVALID_COMMAND);
      }
    }
    else
      process_message (client, message);
  }
}

void Server::process_private(ClientPtr client, const std::string & data) {
  int delimiterPosition = data.find(' ');
  std::string recipientName = data.substr(0, delimiterPosition);
  std::string message = data.substr(delimiterPosition + 1, data.length() - 1);

  ClientPtr recipientClient = find(recipientName);
  if(recipientClient != nullptr)
  {
    recipientClient->write("#private " + client->alias() + " " + message);
  }
}

void Server::process_list (ClientPtr client, const std::string & data)
{
  std::string m = "#list ";
  for (ClientPtr client: m_clients)
  {
    m += client->alias() + " ";
    std::cout << "-> [" << client->alias() << "]" << std::endl;
  }
  m.pop_back();
  client->write(m);
}


void Server::process_message (ClientPtr client, const std::string & data)
{
  std::string m = "<b>" + client->alias() + "</b> : " + data;
  broadcast (m);
}

void Server::process_quit (ClientPtr client, const std::string & data)
{
    m_clients.remove (client);
    client->stop();
}

void Server::process_alias(ClientPtr client, const std::string &data)
{
  std::string newAlias = data;
  client->rename(newAlias);
}

void Server::broadcast (const std::string & message, ClientPtr emitter)
{
    std::string m = message + '\n';
    for (ClientPtr client: this->m_clients) {
      if (client != emitter)
        client->write(message);
    }
}

const std::map<std::string, Server::Processor> Server::PROCESSORS {
  {"/quit", &Server::process_quit},
  {"/list", &Server::process_list},
  {"/private", &Server::process_private},
  {"/alias", &Server::process_alias}
};

const std::string Server::INVALID_ALIAS     {"#error invalid_alias"};
const std::string Server::INVALID_COMMAND   {"#error invalid_command"};
const std::string Server::INVALID_RECIPIENT {"#error invalid_recipient"};
const std::string Server::MISSING_ARGUMENT  {"#error missing_argument"};
