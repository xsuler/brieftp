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
  msg <- recv sock 1024
  let code' = C.unpack $C.take 3 msg
  if read code' == code
    then putStr $"recv "++C.unpack msg
    else putStrLn $ "expect " ++ show code ++ " but recv " ++ code'
  return (sock,C.drop 4 msg)

checkRecv code sock =
  fst<$>withRecv code sock

sendMsg msg sock=
  putStrLn ("sent "++msg)
  >>putStrLn ""
  >>sendAll sock (C.pack (msg++"\r\n"))>>return sock

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
  >>=checkRecv 250

cd dir sock =
  return sock
  >>=sendMsg ("CWD " ++ dir)
  >>=checkRecv 250

list dir sock =
  return sock
  >>=sendMsg ("LIST " ++ dir)
  >>=checkRecv 150

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

recvIO conn= do
      recvLoop
      return ()
    where
      recvLoop=do
        msg <- recv conn 4096
        unless (C.null msg) $ do
          C.putStr msg
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
      checkRecv 226 sock
    case ret of
      Left e->print e
      Right _->return ()
  return sock

handlePasv f addrinfo sock =do
  forkIO $do
    ret<-flip createConnection addrinfo $ \conn-> do
      f conn
      close conn
      checkRecv 226 sock
    case ret of
      Left e->print e
      Right _->return ()
  return sock


stor file sock =
  return sock
  >>=sendMsg ("STOR " ++file)
  >>=checkRecv 150

rm dir sock =
  return sock
  >>=sendMsg ("RMD " ++ dir)
  >>=checkRecv 250

mv old new sock =
  return sock
  >>=sendMsg ("RNFR " ++ old)
  >>=checkRecv 350
  >>=sendMsg ("RNFR " ++ new)
  >>=checkRecv 250

initAction sock=
  return sock
  >>=checkRecv 220
  >>=login "anonymous" "anonymous@gmail.com"
  >>=getSyst
  >>=setType "I"

portAction myHostC act actor sock=do
  (p1,p2)<-((,))<$>(randomRIO (0,255)::IO Int)<*>(randomRIO (0,255)::IO Int)
  readykey<-newEmptyMVar::IO (MVar ())
  action myHostC p1 p2 readykey
  where
    action myHostC p1 p2 readykey=
      return sock
      >>=handlePort readykey actor (show $p1*256+p2)
      >>=port myHostC p1 p2
      >>takeMVar readykey
      >>act sock

pasvAction act actor sock= do
  (p1,p2)<-((,))<$>(randomRIO (0,255)::IO Int)<*>(randomRIO (0,255)::IO Int)
  (sock,info)<-pasv p1 p2 sock
  act sock
  handlePasv actor info sock

portStorAction myHostC file =portAction myHostC (stor file) (sendFile file)
portRetrAction myHostC file =portAction myHostC (retr file) (recvFile file)
portListAction myHostC dir =portAction myHostC (list dir) recvIO

pasvStorAction file =pasvAction (stor file) (sendFile file)
pasvRetrAction file =pasvAction (retr file) (recvFile file)
pasvListAction dir =pasvAction (list dir) recvIO

procAction sock =do
  waitkey<-newEmptyMVar::IO (MVar ())
  action
  takeMVar waitkey
  return sock
  where
    action=
      return sock
      >>=initAction
      >>=portListAction "127,0,0,1" "2.jpeg"

main =do
  ret<-createConnection procAction ("127.0.0.1", "2000")
  case ret of
    Left e->putStr "main error: ">>print e
    Right _->return ()
