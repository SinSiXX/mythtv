#include <qapplication.h>
#include <qsqldatabase.h>
#include <stdlib.h>
#include <iostream>
using namespace std;

#include <qdir.h>

#include "videotree.h"

#include <mythtv/mythcontext.h>
#include <mythtv/mythwidgets.h>
#include <mythtv/uitypes.h>
#include <mythtv/util.h>

VideoTree::VideoTree(MythMainWindow *parent, QSqlDatabase *ldb,
                     QString window_name, QString theme_filename,
                     const char *name)
         : MythThemedDialog(parent, window_name, theme_filename, name)
{
    db = ldb;
    current_parental_level = gContext->GetNumSetting("VideoDefaultParentalLevel", 1);

    file_browser = gContext->GetNumSetting("VideoTreeNoDB", 0);
    browser_mode_files.clear();
        
    //
    //  Theme and tree stuff
    //

    wireUpTheme();
    video_tree_root = new GenericTree("video root", -1, false);
    video_tree_data = video_tree_root->addNode("videos", -1, false);

    buildVideoList();
    
    //  
    //  Tell the tree list to highlight the 
    //  first entry and then display it
    //
    
    video_tree_list->setCurrentNode(video_tree_data);
    if(video_tree_data->childCount() > 0)
    {
        video_tree_list->setCurrentNode(video_tree_data->getChildAt(0, 0));
    }
    updateForeground();
}

VideoTree::~VideoTree()
{
    delete video_tree_root;
}

void VideoTree::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    gContext->GetMainWindow()->TranslateKeyPress("Video", e, actions);

    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "SELECT")
            video_tree_list->select();
        else if (action == "UP")
            video_tree_list->moveUp();
        else if (action == "DOWN")
            video_tree_list->moveDown();
        else if (action == "LEFT")
            video_tree_list->popUp();
        else if (action == "RIGHT")
            video_tree_list->pushDown();
        else if (action == "PAGEUP")
            video_tree_list->pageUp();
        else if (action == "PAGEDOWN")
            video_tree_list->pageDown();
        else if (action == "1" || action == "2" || action == "3" ||
                 action == "4")
        {
            setParentalLevel(action.toInt());
        }
        else
            handled = false;
    }

    if (!handled) 
        MythThemedDialog::keyPressEvent(e);
}

bool VideoTree::checkParentPassword()
{
    QDateTime curr_time = QDateTime::currentDateTime();
    QString last_time_stamp = gContext->GetSetting("VideoPasswordTime");
    QString password = gContext->GetSetting("VideoAdminPassword");
    if(password.length() < 1)
    {
        return true;
    }
    //
    //  See if we recently (and succesfully)
    //  asked for a password
    //
    
    if(last_time_stamp.length() < 1)
    {
        //
        //  Probably first time used
        //

        cerr << "videotree.o: Could not read password/pin time stamp. "
             << "This is only an issue if it happens repeatedly. " << endl;
    }
    else
    {
        QDateTime last_time = QDateTime::fromString(last_time_stamp, Qt::TextDate);
        if(last_time.secsTo(curr_time) < 120)
        {
            //
            //  Two minute window
            //
            last_time_stamp = curr_time.toString(Qt::TextDate);
            gContext->SetSetting("VideoPasswordTime", last_time_stamp);
            gContext->SaveSetting("VideoPasswordTime", last_time_stamp);
            return true;
        }
    }
    
    //
    //  See if there is a password set
    //
    
    if(password.length() > 0)
    {
        bool ok = false;
        MythPasswordDialog *pwd = new MythPasswordDialog(tr("Parental Pin:"),
                                                         &ok,
                                                         password,
                                                         gContext->GetMainWindow());
        pwd->exec();
        delete pwd;
        if(ok)
        {
            //
            //  All is good
            //

            last_time_stamp = curr_time.toString(Qt::TextDate);
            gContext->SetSetting("VideoPasswordTime", last_time_stamp);
            gContext->SaveSetting("VideoPasswordTime", last_time_stamp);
            return true;
        }
    }   
    else
    {
        return true;
    } 
    return false;
}

void VideoTree::setParentalLevel(int which_level)
{
    if(which_level < 1)
    {
        which_level = 1;
    }
    if(which_level > 4)
    {
        which_level = 4;
    }
    
    if(checkParentPassword())
    {
        current_parental_level = which_level;
        pl_value->SetText(QString("%1").arg(current_parental_level));
        video_tree_data->deleteAllChildren();
        buildVideoList();
    
        //  
        //  Tell the tree list to highlight the 
        //  first child and then draw the GUI as
        //  it now stands
        //
    
        video_tree_list->setCurrentNode(video_tree_data);
        if(video_tree_data->childCount() > 0)
        {
            video_tree_list->setCurrentNode(video_tree_data->getChildAt(0, 0));
        }
        updateForeground();
    }
}

bool VideoTree::ignoreExtension(QString extension)
{
    QString q_string = QString("SELECT f_ignore FROM videotypes WHERE extension = \"%1\" ;")
                              .arg(extension);

    QSqlQuery a_query(q_string, db);
    if(a_query.isActive() && a_query.numRowsAffected() > 0)
    {
        a_query.next();
        return a_query.value(0).toBool();
    }
    
    return !gContext->GetNumSetting("VideoListUnknownFileTypes", 1);

}

void VideoTree::buildFileList(QString directory)
{
    QDir d(directory);

    if (!d.exists())
        return;

    const QFileInfoList *list = d.entryInfoList();
    if (!list)
        return;

    QFileInfoListIterator it(*list);
    QFileInfo *fi;
    QRegExp r;

    while ((fi = it.current()) != 0)
    {
        ++it;
        if (fi->fileName() == "." || 
            fi->fileName() == ".." ||
            fi->fileName() == "Thumbs.db")
        {
            continue;
        }
        
        if(!fi->isDir())
        {
            if(ignoreExtension(fi->extension(false)))
            {
                continue;
            }
        }
        
        QString filename = fi->absFilePath();
        if (fi->isDir())
            buildFileList(filename);
        else
        {
            browser_mode_files.append(filename);
        }
    }
}

void VideoTree::buildVideoList()
{
    if(file_browser)
    {
        //
        //  Fill metadata from directory structure
        //
        
        buildFileList(gContext->GetSetting("VideoStartupDir"));

        for(uint i=0; i < browser_mode_files.count(); i++)
        {
            QString file_string = *(browser_mode_files.at(i));
            QString prefix = gContext->GetSetting("VideoStartupDir");
            if(prefix.length() < 1)
            {
                cerr << "videotree.o: Seems unlikely that this is going to work" << endl;
            }
            file_string.remove(0, prefix.length());
            QStringList list(QStringList::split("/", file_string));

            GenericTree *where_to_add;
            where_to_add = video_tree_data;
            int a_counter = 0;
            QStringList::Iterator an_it = list.begin();
            for( ; an_it != list.end(); ++an_it)
            {
                if(a_counter + 1 >= (int) list.count())
                {
                    QString title = (*an_it);
                    where_to_add->addNode(title.section(".",0,0), i, true);
                    
                }
                else
                {
                    QString dirname = *an_it + "/";
                    GenericTree *sub_node;
                    sub_node = where_to_add->getChildByName(dirname);
                    if(!sub_node)
                    {
                        sub_node = where_to_add->addNode(dirname, -1, false);
                    }
                    where_to_add = sub_node;
                }
                ++a_counter;
            }
        }
        if(video_tree_data->childCount() < 1)
        {
            //
            //  Nothing survived the requirements
            //

            video_tree_data->addNode(tr("No files found"), -1, false);
        }        
    }
    else
    {
        //
        //  This just asks the database for a list
        //  of metadata, builds it into a tree and
        //  passes that tree structure to the GUI
        //  widget that handles navigation
        //

        QSqlQuery query("SELECT intid FROM videometadata ;", db);
        Metadata *myData;
    
        if(!query.isActive())
        {
            cerr << "videotree.o: Your database sucks" << endl;
        }
        else if (query.numRowsAffected() > 0)
        {
            while (query.next())
            {
                unsigned int idnum = query.value(0).toUInt();

                //
                //  Create metadata object and fill it
                //
            
                myData = new Metadata();
                myData->setID(idnum);
                myData->fillDataFromID(db);
                if (myData->ShowLevel() <= current_parental_level && myData->ShowLevel() != 0)
                {
                    QString file_string = myData->Filename();
                    QString prefix = gContext->GetSetting("VideoStartupDir");
                    if(prefix.length() < 1)
                    {
                        cerr << "videotree.o: Seems unlikely that this is going to work" << endl;
                    }
                    file_string.remove(0, prefix.length());
                    QStringList list(QStringList::split("/", file_string));

                    GenericTree *where_to_add;
                    where_to_add = video_tree_data;
                    int a_counter = 0;
                    QStringList::Iterator an_it = list.begin();
                    for( ; an_it != list.end(); ++an_it)
                    {
                        if(a_counter + 1 >= (int) list.count())
                        {
                            where_to_add->addNode(myData->Title(), idnum, true);                    
                        }
                        else
                        {
                            QString dirname = *an_it + "/";
                            GenericTree *sub_node;
                            sub_node = where_to_add->getChildByName(dirname);
                            if(!sub_node)
                            {
                                sub_node = where_to_add->addNode(dirname, 0, false);
                            }
                            where_to_add = sub_node;
                        }
                        ++a_counter;
                    }
                }
            
                //
                //  Kill it off
                //
            
                delete myData;
            }
        }
        else
        {
            video_tree_data->addNode(tr("No files found"), -1, false);
        }
    }
    
    video_tree_list->assignTreeData(video_tree_root);
    video_tree_list->sortTreeByString();
    video_tree_list->sortTreeBySelectable();
}


void VideoTree::handleTreeListEntry(int node_int, IntVector*)
{
    if(node_int >= 0)
    {
        //
        //  User has navigated to a video file
        //
        
        QString extension = "";
        QString player = "";
        QString unique_player;
            
        if(file_browser)
        {
            if(node_int >= (int) browser_mode_files.count())
            {
                cerr << "videotree.o: Uh Oh. Reference larger than count of browser_mode_files" << endl;
                
            }
            else
            {
                QString the_file = *(browser_mode_files.at(node_int));
                QString base_name = the_file.section("/", -1);
                video_title->SetText(base_name.section(".", 0, -2));
                video_file->SetText(base_name);
                extension = the_file.section(".", -1);
                player = gContext->GetSetting("VideoDefaultPlayer");
            }
        }
        else
        {
            Metadata *node_data;
            node_data = new Metadata();
            node_data->setID(node_int);
            node_data->fillDataFromID(db);
            video_title->SetText(node_data->Title());
            video_file->SetText(node_data->Filename().section("/", -1));
            video_poster->SetImage(node_data->CoverFile());
            video_poster->LoadImage();
            extension = node_data->Filename().section(".", -1, -1);
            unique_player = node_data->PlayCommand();
            if(unique_player.length() > 0)
            {
                player = unique_player;
            }
            else
            {
                player = gContext->GetSetting("VideoDefaultPlayer");
            }
            delete node_data;
        }

        //
        //  Find the player for this file
        //
        


        QString q_string = QString("SELECT playcommand, use_default FROM "
                                   "videotypes WHERE extension = \"%1\" ;")
                                   .arg(extension);

        QSqlQuery a_query(q_string, db);
        
        if(a_query.isActive() && a_query.numRowsAffected() > 0)
        {
            a_query.next();
            if(!a_query.value(1).toBool() && unique_player.length() < 1)
            {
                //
                //  This file type is defined and
                //  it is not set to use default player
                //
                player = a_query.value(0).toString();                
            }
        }

        
        video_player->SetText(player); 
    }
    else
    {
        //
        //  not a leaf node 
        //  (no video file here, just a directory)
        //
        
        video_title->SetText("");
        video_file->SetText("");
        video_player->SetText(""); 
        
    }
    
}

void VideoTree::playVideo(int node_number)
{
    if(node_number > 0)
    {
        //
        //  User has selected a video file
        //
    
        QString filename = "";
        QString handler = gContext->GetSetting("VideoDefaultPlayer");
        QString unique_handler = "";
        
        if(file_browser)
        {
            filename = *(browser_mode_files.at(node_number));
        }
        else
        {
            Metadata *node_data;
            node_data = new Metadata();
            node_data->setID(node_number);
            node_data->fillDataFromID(db);
            filename = node_data->Filename();
            
            //
            //  Player command unique to this file?
            //

            unique_handler = node_data->PlayCommand();
            if(unique_handler.length() > 0)
            {
                handler = unique_handler;
            }
            delete node_data;
        }
        

        //
        //  Do we have a specialized player for this
        //  type of file?
        //
        
        QString extension = filename.section(".", -1, -1);

        QString q_string = QString("SELECT playcommand, use_default FROM "
                                   "videotypes WHERE extension = \"%1\" ;")
                                   .arg(extension);

        QSqlQuery a_query(q_string, db);
        
        if( a_query.isActive() && 
            a_query.numRowsAffected() > 0 && 
            unique_handler.length() < 1)
        {
            a_query.next();
            if(!a_query.value(1).toBool())
            {
                //
                //  This file type is defined and
                //  it is not set to use default player
                //
                handler = a_query.value(0).toString();                
            }
        }

        QString arg;
        arg.sprintf("\"%s\"", 
                    filename.replace(QRegExp("\""), "\\\"").utf8().data());

        //
        //  Did the user specify %s in the player
        //  command?
        //

        QString command = "";
        if(handler.contains("%s"))
        {
            command = handler.replace(QRegExp("%s"), arg);
        }
        else
        {
            command = handler + " " + arg;
        }

        // cout << "command:" << command << endl;
        
        //
        //  Run the player
        //
        
        myth_system((QString("%1 ").arg(command)).local8Bit());

        
    }
}


void VideoTree::handleTreeListSelection(int node_int, IntVector *)
{
    if(node_int >= 0)
    {
        //
        //  Play this (chain of?) file(s) 
        //
        
        int which_file = node_int;
        QTime playing_time;

        while(which_file > 0)
        {
            playing_time.start();
            playVideo(which_file);
            if(!file_browser)
            {
                if(playing_time.elapsed() > 10000)
                {
                    //
                    //  More than ten seconds have gone by,
                    //  so we'll keep following the chain of
                    //  files to play
                    //

                    Metadata *node_data = new Metadata();
                    node_data->setID(which_file);
                    node_data->fillDataFromID(db);
                    which_file = node_data->ChildID();
                    cout << "Just set which file to " << which_file << endl;
                    delete node_data;
                }
                else
                {
                    which_file = 0;
                }
            }
            else
            {
                which_file = 0;
            }
        }
        
        //
        //  Go back to tree browsing
        //

        video_tree_list->deactivate();
        gContext->GetMainWindow()->raise();
        gContext->GetMainWindow()->setActiveWindow();
        gContext->GetMainWindow()->currentWidget()->setFocus();
    }
}

void VideoTree::wireUpTheme()
{
    //
    //  Use inherited getUI* methods to hook pointers
    //  to objects on the screen we need to be able to
    //  control
    //
    
    //
    //  Big tree thingy at the top
    //
    
    video_tree_list = getUIManagedTreeListType("videotreelist");
    if(!video_tree_list)
    {
        cerr << "videotree.o: Couldn't find a video tree list in your theme" << endl;
        exit(0);
    }
    video_tree_list->showWholeTree(true);
    video_tree_list->colorSelectables(true);
    connect(video_tree_list, SIGNAL(nodeSelected(int, IntVector*)), this, SLOT(handleTreeListSelection(int, IntVector*)));
    connect(video_tree_list, SIGNAL(nodeEntered(int, IntVector*)), this, SLOT(handleTreeListEntry(int, IntVector*)));

    //
    //  Text area's
    //
    video_title = getUITextType("video_title");
    if(!video_title)
    {
        cerr << "videotree.o: Couldn't find a text area called video_title in your theme" << endl;
    }
    video_file = getUITextType("video_file");
    if(!video_file)
    {
        cerr << "videotree.o: Couldn't find a text area called video_file in your theme" << endl;
    }
    video_player = getUITextType("video_player");
    if(!video_player)
    {
        cerr << "videotree.o: Couldn't find a text area called video_player in your theme" << endl;
    }
    
    //
    //  Cover Art
    //
    video_poster = getUIImageType("video_poster");
    if(!video_poster)
    {
        cerr << "videotree.o: Couldn't find an image called video_poster in your theme" << endl;
    }
    
    //
    //  Status of Parental Level
    //
    
    pl_value = getUITextType("pl_value");
    if(pl_value)
    {
        pl_value->SetText(QString("%1").arg(current_parental_level));
    }
    
}

