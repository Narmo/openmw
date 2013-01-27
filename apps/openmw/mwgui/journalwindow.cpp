#include "journalwindow.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "list.hpp"

#include <sstream>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include "boost/lexical_cast.hpp"

#include "bookpage.hpp"
#include "windowbase.hpp"
#include "imagebutton.hpp"
#include "journalviewmodel.hpp"
#include "journalbooks.hpp"

using namespace MyGUI;
using namespace MWGui;

namespace
{
#define CONTROL_ID(name)    \
    static char const name [] = #name;

    CONTROL_ID(OptionsOverlay)

    CONTROL_ID(OptionsBTN)
    CONTROL_ID(PrevPageBTN);
    CONTROL_ID(NextPageBTN);
    CONTROL_ID(CloseBTN);
    CONTROL_ID(JournalBTN);
    CONTROL_ID(TopicsBTN);
    CONTROL_ID(QuestsBTN);
    CONTROL_ID(CancelBTN);
    CONTROL_ID(ShowAllBTN);
    CONTROL_ID(ShowActiveBTN);
    CONTROL_ID(PageOneNum)
    CONTROL_ID(PageTwoNum)
    CONTROL_ID(TopicsList);
    CONTROL_ID(TopicsPage);
    CONTROL_ID(QuestsList);
    CONTROL_ID(QuestsPage);
    CONTROL_ID(LeftBookPage);
    CONTROL_ID(RightBookPage);
    CONTROL_ID(LeftTopicIndex);
    CONTROL_ID(RightTopicIndex);

    struct JournalWindow : WindowBase, JournalBooks, IJournalWindow
    {
        struct display_state
        {
            int mPage;
            book mBook;
        };

        typedef std::stack <display_state> display_state_stack;

        display_state_stack mStates;
        book mTopicIndexBook;
        bool mQuestMode;
        bool mAllQuests;

        template <typename widget_type>
        widget_type * getWidget (char const * name)
        {
            widget_type * widget;
            WindowBase::getWidget (widget, name);
            return widget;
        }

        template <typename value_type>
        void setText (char const * name, value_type const & value)
        {
            getWidget <TextBox> (name) ->
                setCaption (boost::lexical_cast <std::string> (value));
        }

        void setVisible (char const * name, bool visible)
        {
            getWidget <Widget> (name) ->
                setVisible (visible);
        }

        void adviseButtonClick (char const * name, void (JournalWindow::*Handler) (Widget* _sender))
        {
            getWidget <MWGui::ImageButton> (name) ->
                eventMouseButtonClick += newDelegate(this, Handler);
        }

        MWGui::IBookPage* getPage (char const * name)
        {
            return getWidget <MWGui::IBookPage> (name);
        }

        JournalWindow (IJournalViewModel::ptr Model)
            : WindowBase("openmw_journal.layout"), JournalBooks (Model)
        {
            mMainWidget->setVisible(false);
            center();

            adviseButtonClick (OptionsBTN,    &JournalWindow::notifyOptions   );
            adviseButtonClick (PrevPageBTN,   &JournalWindow::notifyPrevPage  );
            adviseButtonClick (NextPageBTN,   &JournalWindow::notifyNextPage  );
            adviseButtonClick (CloseBTN,      &JournalWindow::notifyClose     );
            adviseButtonClick (JournalBTN,    &JournalWindow::notifyJournal   );

            adviseButtonClick (TopicsBTN,     &JournalWindow::notifyTopics    );
            adviseButtonClick (QuestsBTN,     &JournalWindow::notifyQuests    );
            adviseButtonClick (CancelBTN,     &JournalWindow::notifyCancel    );

            adviseButtonClick (ShowAllBTN,    &JournalWindow::notifyShowAll   );
            adviseButtonClick (ShowActiveBTN, &JournalWindow::notifyShowActive);

            {
                auto callback = std::bind (&JournalWindow::notifyTopicClicked, this, std::placeholders::_1);

                getPage (TopicsPage)->adviseLinkClicked (callback);
                getPage (LeftBookPage)->adviseLinkClicked (callback);
                getPage (RightBookPage)->adviseLinkClicked (callback);
            }

            {
                auto callback = std::bind (&JournalWindow::notifyIndexLinkClicked, this, std::placeholders::_1);

                getPage (LeftTopicIndex)->adviseLinkClicked (callback);
                getPage (RightTopicIndex)->adviseLinkClicked (callback);
            }

            {
                auto callback = std::bind (&JournalWindow::notifyQuestClicked, this, std::placeholders::_1);

                getPage (QuestsPage)->adviseLinkClicked (callback);
            }

            mQuestMode = false;
            mAllQuests = false;
        }

        void open()
        {
            Model->load ();

            setBookMode ();

            MWBase::Environment::get().getSoundManager()->playSound ("book open", 1.0, 1.0);

            book journalBook;
            if (Model->is_empty ())
                journalBook = createEmptyJournalBook ();
            else
                journalBook = createJournalBook ();

            pushBook (journalBook, 0);
        }

        void close()
        {
            Model->unload ();

            getPage (LeftBookPage)->showPage (book (), 0);
            getPage (RightBookPage)->showPage (book (), 0);

            decltype (mStates) clr;
            mStates.swap (clr);

            mTopicIndexBook.reset ();

            MWBase::Environment::get().getSoundManager()->playSound ("book close", 1.0, 1.0);
        }

        void setVisible (bool newValue)
        {
            WindowBase::setVisible (newValue);
        }

        void setBookMode ()
        {
            setVisible (OptionsBTN, true);
            setVisible (OptionsOverlay, false);

            updateShowingPages ();
            updateCloseJournalButton ();
        }

        void setOptionsMode ()
        {
            setVisible (OptionsBTN, false);
            setVisible (OptionsOverlay, true);

            setVisible (PrevPageBTN, false);
            setVisible (NextPageBTN, false);
            setVisible (CloseBTN, false);
            setVisible (JournalBTN, false);

            setVisible (TopicsList, false);
            setVisible (QuestsList, mQuestMode);
            setVisible (LeftTopicIndex, !mQuestMode);
            setVisible (RightTopicIndex, !mQuestMode);
            setVisible (ShowAllBTN, mQuestMode && !mAllQuests);
            setVisible (ShowActiveBTN, mQuestMode && mAllQuests);

            //TODO: figure out how to make "options" page overlay book page
            //      correctly, so that text may show underneath
            getPage (RightBookPage)->showPage (book (), 0);
        }

        void pushBook (book Book, int Page)
        {
            display_state bs;
            bs.mPage = Page;
            bs.mBook = Book;
            mStates.push (bs);
            updateShowingPages ();
            updateCloseJournalButton ();
        }

        void replaceBook (book Book, int Page)
        {
            assert (!mStates.empty ());
            mStates.top ().mBook = Book;
            mStates.top ().mPage = Page;
            updateShowingPages ();
        }

        void popBook ()
        {
            mStates.pop ();
            updateShowingPages ();
            updateCloseJournalButton ();
        }

        void updateCloseJournalButton ()
        {
            setVisible (CloseBTN, mStates.size () < 2);
            setVisible (JournalBTN, mStates.size () >= 2);
        }

        void updateShowingPages ()
        {
            book Book;
            int Page;
            int relPages;

            if (!mStates.empty ())
            {
                Book = mStates.top ().mBook;
                Page = mStates.top ().mPage;
                relPages = Book->pageCount () - Page;
            }
            else
            {
                Page = 0;
                relPages = 0;
            }

            setVisible (PrevPageBTN, Page > 0);
            setVisible (NextPageBTN, relPages > 2);

            setVisible (PageOneNum, relPages > 0);
            setVisible (PageTwoNum, relPages > 1);

            getPage (LeftBookPage)->showPage ((relPages > 0) ? Book : book (), Page+0);
            getPage (RightBookPage)->showPage ((relPages > 0) ? Book : book (), Page+1);

            setText (PageOneNum, Page + 1);
            setText (PageTwoNum, Page + 2);
        }

        void notifyTopicClicked (intptr_t linkId)
        {
            auto topicBook = createTopicBook (linkId);

            if (mStates.size () > 1)
                replaceBook (topicBook, 0);
            else
                pushBook (topicBook, 0);

            setVisible (OptionsOverlay, false);
            setVisible (OptionsBTN, true);
            setVisible (JournalBTN, true);
        }

        void notifyQuestClicked (intptr_t questId)
        {
            auto Book = createQuestBook (questId);

            if (mStates.size () > 1)
                replaceBook (Book, 0);
            else
                pushBook (Book, 0);

            setVisible (OptionsOverlay, false);
            setVisible (OptionsBTN, true);
            setVisible (JournalBTN, true);
        }

        void notifyOptions(Widget* _sender)
        {
            setOptionsMode ();

            if (!mTopicIndexBook)
                mTopicIndexBook = createTopicIndexBook ();

            getPage (LeftTopicIndex)->showPage (mTopicIndexBook, 0);
            getPage (RightTopicIndex)->showPage (mTopicIndexBook, 1);
        }

        void notifyJournal(Widget* _sender)
        {
            assert (mStates.size () > 1);
            popBook ();
        }

        void showList (char const * ListId, char const * PageId, book book)
        {
            auto size = book->getSize ();

            getPage (PageId)->showPage (book, 0);

            getWidget <ScrollView> (ListId)->setCanvasSize (size.first, size.second);
        }

        void notifyIndexLinkClicked (ITypesetBook::interactive_id character)
        {
            setVisible (LeftTopicIndex, false);
            setVisible (RightTopicIndex, false);
            setVisible (TopicsList, true);

            showList (TopicsList, TopicsPage, createTopicIndexBook ((char)character));
        }

        void notifyTopics(Widget* _sender)
        {
            mQuestMode = false;
            setVisible (LeftTopicIndex, true);
            setVisible (RightTopicIndex, true);
            setVisible (TopicsList, false);
            setVisible (QuestsList, false);
            setVisible (ShowAllBTN, false);
            setVisible (ShowActiveBTN, false);
        }

        void notifyQuests(Widget* _sender)
        {
            mQuestMode = true;
            setVisible (LeftTopicIndex, false);
            setVisible (RightTopicIndex, false);
            setVisible (TopicsList, false);
            setVisible (QuestsList, true);
            setVisible (ShowAllBTN, !mAllQuests);
            setVisible (ShowActiveBTN, mAllQuests);

            showList (QuestsList, QuestsPage, createQuestIndexBook (!mAllQuests));
        }

        void notifyShowAll(Widget* _sender)
        {
            mAllQuests = true;
            setVisible (ShowAllBTN, !mAllQuests);
            setVisible (ShowActiveBTN, mAllQuests);
            showList (QuestsList, QuestsPage, createQuestIndexBook (!mAllQuests));
        }

        void notifyShowActive(Widget* _sender)
        {
            mAllQuests = false;
            setVisible (ShowAllBTN, !mAllQuests);
            setVisible (ShowActiveBTN, mAllQuests);
            showList (QuestsList, QuestsPage, createQuestIndexBook (!mAllQuests));
        }

        void notifyCancel(Widget* _sender)
        {
            setBookMode ();
        }

        void notifyClose(Widget* _sender)
        {

            MWBase::Environment::get().getWindowManager ()->popGuiMode ();
        }

        void notifyNextPage(Widget* _sender)
        {
            if (!mStates.empty ())
            {
                auto & Page = mStates.top ().mPage;
                auto   Book = mStates.top ().mBook;

                if (Page < Book->pageCount () - 2)
                {
                    Page += 2;
                    updateShowingPages ();
                }
            }
        }

        void notifyPrevPage(Widget* _sender)
        {
            if (!mStates.empty ())
            {
                auto & Page = mStates.top ().mPage;

                if(Page > 0)
                {
                    Page -= 2;
                    updateShowingPages ();
                }
            }
        }
    };
}

// glue the implementation to the interface
IJournalWindow * MWGui::IJournalWindow::create (IJournalViewModel::ptr Model)
{
    return new JournalWindow (Model);
}
