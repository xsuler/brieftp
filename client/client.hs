{-# LANGUAGE OverloadedStrings #-}

module Main
  ( main
  ) where

import qualified Control.Exception as E
import qualified Data.ByteString.Char8 as C
import Network.Socket hiding (recv)
import Network.Socket.ByteString (recv, sendAll)
import System.Random(randomRIO)
import System.IO
import Control.Monad
import Data.List.Split(splitOn)
import Data.Foldable(fold)
import Control.Concurrent(forkIO)
import Control.Concurrent.MVar
import System.Console.Haskeline
import Control.Monad.Trans (liftIO)

newConnection::String->String->IO Socket
newConnection host port=do
  addr <- resolve host port
  open addr
  where
    resolve host port = do
      let hints = defaultHints {addrSocketType = Stream, addrProtocol=0}
      addr:_ <- getAddrInfo (Just hints) (Just host) (Just port)
      return addr
    open addr = do
          sock <- socket (addrFamily addr) (addrSocketType addr) (addrProtocol addr)
          connect sock $ addrAddress addr
          return sock


createConnection::(Socket->IO Socket)->(String,String)->IO (Either E.IOException Socket)
createConnection talk (host,port) =
  withSocketsDo $ do
    addr <- resolve host port
    E.bracket (open addr) close (E.try<$>talk)
  where
    resolve host port = do
      let hints = defaultHints {addrSocketType = Stream, addrProtocol=0}
      addr:_ <- getAddrInfo (Just hints) (Just host) (Just port)
      return addr
    open addr = do
          sock <- socket (addrFamily addr) (addrSocketType addr) (addrProtocol addr)
          connect sock $ addrAddress addr
          return sock

createListen::(Socket->IO Socket)->String->IO (Either E.IOException Socket)
createListen talk port = withSocketsDo $ do
    addr <- resolve port
    E.bracket (open addr) close (E.try<$>talk)
  where
    resolve port = do
        let hints = defaultHints {
                addrFlags = [AI_PASSIVE]
              , addrSocketType = Stream
              , addrProtocol = 6
              }
        addr:_ <- getAddrInfo (Just hints) Nothing (Just port)
        return addr
    open addr = do
            sock <- socket (addrFamily addr) (addrSocketType addr) (addrProtocol addr)
            setSocketOption sock ReuseAddr 1
            bind sock (addrAddress addr)
            let fd = fdSocket sock
            setCloseOnExecIfNeeded fd
            listen sock 10
            return sock


withRecv code sock =do
  msg <- liftIO$recv sock 1024
  let code' = C.unpack $C.take 3 msg
  if read code' == code
    then return ()
    else outputStrLn $ "error " ++ C.unpack msg
  return (sock,C.drop 4 msg)

checkRecv code sock =  fst<$>withRecv code sock

sendMsg msg sock=
  liftIO (sendAll sock (C.pack (msg++"\r\n"))>>return sock)


login user pass sock =
  return sock
  >>=sendMsg ("USER "++user)
  >>=checkRecv 331
  >>=sendMsg ("PASS "++pass)
  >>=checkRecv 230

getSyst sock =
  return sock
  >>=sendMsg "SYST "
  >>=checkRecv 215

setType t sock =
  return sock
  >>=sendMsg ("TYPE " ++ t)
  >>=checkRecv 200

mkdir dir sock =
  return sock
  >>=sendMsg ("MKD " ++ dir)
  >>=checkRecv 257
  >>=checkRecv 250

cd dir sock =
  return sock
  >>=sendMsg ("CWD " ++ dir)
  >>=checkRecv 250

list dir sock =
  return sock
  >>=sendMsg ("LIST " ++ dir)
  >>=checkRecv 150

quit sock =
  return sock
  >>=sendMsg "QUIT "
  >>=checkRecv 221

pwd sock =
  return sock
  >>=sendMsg "PWD "
  >>=withRecv 250

pasv p1 p2 sock =
  return sock
  >>=sendMsg "PASV "
  >>=withRecv 227
  >>= \(sock,msg)-> return (sock,parseAddr $C.unpack msg)
  where
    parseAddr addr=
      let ls=splitOn "," addr
          host=foldl (\s t->s++"."++t) (head ls) $ take 3 $ tail ls
          port= read (last $init ls)*256+(read $last ls)
      in (host,show port)

port myHostC p1 p2 sock =
  return sock
  >>=sendMsg ("PORT " ++ myHostC ++ ","++show p1++","++show p2++" ")
  >>=checkRecv 200


retr file sock =
  return sock
  >>=sendMsg ("RETR " ++file)
  >>=checkRecv 150

recvIO printer conn= do
      recvLoop
      return ()
    where
      recvLoop=do
        msg <- liftIO $recv conn 4096
        unless (C.null msg) $ do
          printer $C.unpack msg
          recvLoop
        return ()

recvFile file conn=
    withBinaryFile file WriteMode $ \handle -> do
      recvLoop handle
      return ()
    where
      recvLoop handle=do
        msg <- recv conn 4096
        unless (C.null msg) $ do
          C.hPutStr handle msg
          recvLoop handle

sendFile file conn=
    withBinaryFile file ReadMode $ \handle -> do
      sendLoop handle
      return ()
    where
      sendLoop handle=do
        msg <- C.hGet handle 4096
        unless (C.null msg) $ do
          sendAll conn msg
          sendLoop handle

handlePort readykey f port sock = do
  forkIO $ do
    ret<-flip createListen port $ \conn-> do
      putMVar readykey ()
      (connt, _ ) <- accept conn
      f connt
      close connt
      close conn
      runInputT defaultSettings $checkRecv 226 sock
    case ret of
      Left e->print e
      Right _->return ()
  return sock

handlePasv f addrinfo sock =do
  forkIO $do
    ret<-flip createConnection addrinfo $ \conn-> do
      f conn
      close conn
      runInputT defaultSettings $checkRecv 226 sock
    case ret of
      Left e->print e
      Right _->return ()
  return sock


stor file sock =
  return sock
  >>=sendMsg ("STOR " ++file)
  >>=checkRecv 150

rmdir dir sock =
  return sock
  >>=sendMsg ("RMD " ++ dir)
  >>=checkRecv 250

rmfile file sock =
  return sock
  >>=sendMsg ("DELE " ++ file)
  >>=checkRecv 250


mv old new sock =
  return sock
  >>=sendMsg ("RNFR " ++ old)
  >>=checkRecv 350
  >>=sendMsg ("RNFR " ++ new)
  >>=checkRecv 250


initAction env sock= do
  checkRecv 220 sock
  str<-getInputLineWithInitial "Enter username: " ("anonymous","")
  case str of
    Nothing->return env
    Just name->do
      pswx<-getPassword (Just '*') "Enter password: "
      case pswx of
        Nothing ->return env
        Just psw->
          return sock
          >>=login name psw
          >>=getSyst
          >>=setType "I"
          >>return env{name=name}


portAction myHostC act actor sock=do
  (p1,p2)<-liftIO $ ((,))<$>(randomRIO (0,255)::IO Int)<*>(randomRIO (0,255)::IO Int)
  readykey<-liftIO $ newEmptyMVar::InputT IO (MVar ())
  action myHostC p1 p2 readykey
  where
    action myHostC p1 p2 readykey=
      return sock
      >>liftIO (handlePort readykey actor (show $p1*256+p2) sock)
      >>=port myHostC p1 p2
      >>liftIO (takeMVar readykey)
      >>act sock

pasvAction act actor sock= do
  (p1,p2)<-liftIO $((,))<$>(randomRIO (0,255)::IO Int)<*>(randomRIO (0,255)::IO Int)
  (sock,info)<-pasv p1 p2 sock
  act sock
  liftIO $handlePasv actor info sock

portStorAction myHostC file =portAction myHostC (stor file) (sendFile file)
portRetrAction myHostC file =portAction myHostC (retr file) (recvFile file)
portListAction printer myHostC dir =portAction myHostC (list dir) (recvIO printer)

pasvStorAction file =pasvAction (stor file) (sendFile file)
pasvRetrAction file =pasvAction (retr file) (recvFile file)
pasvListAction printer dir =pasvAction (list dir)$ recvIO printer


rmdirAction env file = rmdir file $fromJust$ sock env
rmfileAction env file = rmfile file $fromJust$ sock env
mvAction env old new = mv old new $fromJust$ sock env
mkdirAction env dir = mkdir dir $fromJust$ sock env
dropAction env =do
  case sock env of
    Nothing -> return env
    Just socket ->quit socket>>return env

cdAction env dir =do
  let socket=fromJust$ sock env
  return socket
  >>=cd dir
  >>=pwd
  >>= \(_,msg)->return env{path=strip $C.unpack msg}

strip = fix.filter (\c->(c/='\r')&&(c/='\n'))
  where
    fix str
     |null str = "/"
     |otherwise = str

main=client

data Command = Command Func [Param]

type Param = String

data Func = NoAct|Connect|Ls|Retr|Stor|Mv|Cd|Rmdir|Rmfile|Mkdir|Drop|Help|Use

data Env = Env{ sock::(Maybe Socket),host:: String,name:: String,mode::Mode,myHostC::String,path::String}

data Mode = Port|Pasv

parseCommand = parse.words
  where
    parse ls
     | null ls =Command NoAct []
     | otherwise = Command (toFunc (head ls)) $tail ls

toFunc str
 |str=="connect" = Connect
 |str=="ls" = Ls
 |str=="retr" = Retr
 |str=="stor" = Stor
 |str=="mv" = Mv
 |str=="cd" = Cd
 |str=="rmdir" = Rmdir
 |str=="rmfile" = Rmfile
 |str=="mkdir" = Mkdir
 |str=="drop" = Drop
 |str==":h" = Help
 |str==":use" = Use
 |otherwise = NoAct

fromJust (Just s) = s

runCommand env (Command NoAct _) = outputStrLn "command not exist">>return env

runCommand env (Command Help _) = outputStr helpstr>>return env

runCommand env (Command Mv []) = outputStrLn "need two argument">>return env
runCommand env (Command Mv (_:[])) = outputStrLn "need two argument">>return env
runCommand env (Command Mv params) = mvAction env (head params) (head $tail params)>>return env

runCommand env (Command Cd []) = outputStrLn "need one argument">>return env
runCommand env (Command Cd params) = cdAction env (head params)

runCommand env (Command Rmdir []) = outputStrLn "need one argument">>return env
runCommand env (Command Rmdir params) = rmdirAction env (head params)>>return env

runCommand env (Command Rmfile []) = outputStrLn "need one argument">>return env
runCommand env (Command Rmfile params) = rmfileAction env (head params)>>return env

runCommand env (Command Mkdir []) = outputStrLn "need one argument">>return env
runCommand env (Command Mkdir params) = mkdirAction env (head params)>>return env

runCommand env (Command Drop _) = dropAction env

runCommand env (Command Stor []) = outputStrLn "need one argument">>return env
runCommand env (Command Stor params) = do
  let file= head params
  case mode env of
    Pasv->pasvStorAction file (fromJust $sock env)
    Port->portStorAction (myHostC env) file (fromJust $sock env)
  return env

runCommand env (Command Retr []) = outputStrLn "need one argument">>return env
runCommand env (Command Retr params) = do
  let file= head params
  case mode env of
    Pasv->pasvRetrAction file (fromJust $sock env)
    Port->portRetrAction (myHostC env) file (fromJust $sock env)
  return env

runCommand env (Command Ls params) = do
  let file= if null params then "." else head params
  printer<-getExternalPrint
  case mode env of
    Pasv->pasvListAction printer file (fromJust $sock env)
    Port->portListAction printer (myHostC env) file (fromJust $sock env)
  return env

runCommand env (Command Connect []) = outputStrLn "need two argument">>return env
runCommand env (Command Connect (_:[])) = outputStrLn "need two argument">>return env
runCommand env (Command Connect params) = do
   ret<-liftIO $E.try <$>newConnection (head params) $head $tail params
   case ret of
    Left e->do
      outputStrLn $ "error connection "++ignore e
      return env
    Right sock->
      initAction env{sock=(Just sock),host=(head params),mode=Pasv,path="/"} sock

ignore::E.IOException->String
ignore _= ""

reply env input
 | null input = return env
 | otherwise =runCommand env $ parseCommand input

defaultEnv = Env Nothing "" "" Pasv "127,0,0,1" ""

client = runInputT defaultSettings $loop defaultEnv
  where
       loop env = do
           minput <- getInputLine $name env++"@"++host env++" "++path env++">>= "
           case minput of
               Nothing -> dropAction env
               Just ":q" -> dropAction env
               Just input -> do nenv<-reply env input
                                loop nenv

helpstr =
  "helper commands:\n\n\
  \\t:h\t get help\n\
  \\t:q\t quit\n"

