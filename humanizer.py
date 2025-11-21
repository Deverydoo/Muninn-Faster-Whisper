"""
AI Content Humanizer - Remove AI telltale signs from generated text

This module processes AI-generated content to make it sound more human-written
by removing common patterns, word choices, and structures that reveal AI authorship.

Usage:
    from yt_humanizer import humanize_text

    # Quick humanization
    text = humanize_text("Let's delve into this topic — it's worth noting that...")
    # Output: "Let's explore this topic. It's important that..."

    # Content-specific humanization
    title = humanize_text(title, content_type='youtube_title', aggressive=True)
    description = humanize_text(desc, content_type='youtube_description')

Author: Loki Studio
License: MIT
"""

import re
from typing import Dict, List, Tuple


class ContentHumanizer:
    """Remove AI telltale signs from text to make it sound more human-written"""

    def __init__(self, aggressive: bool = False):
        """
        Initialize humanizer with configuration

        Args:
            aggressive: If True, apply more transformations (adds contractions,
                       removes more phrases). May change tone slightly.
        """
        self.aggressive = aggressive

        # Word replacements (AI word -> Human alternative)
        self.word_replacements = {
            # High-priority AI tells
            r'\bdelve(?:s|d)?\b': 'explore',
            r'\bdelving\b': 'exploring',
            r'\bleverage(?:s|d)?\b': 'use',
            r'\bleveraging\b': 'using',
            r'\butilize(?:s|d)?\b': 'use',
            r'\butilizing\b': 'using',

            # Overly formal transitions
            r'\bmoreover\b': 'also',
            r'\bfurthermore\b': 'also',
            r'\badditionally\b': 'plus',
            r'\bhence\b': 'so',
            r'\bthus\b': 'so',

            # Marketing buzzwords
            r'\bunlock(?:s|ed|ing)?\b': 'discover',
            r'\bharness(?:es|ed|ing)?\b': 'use',
            r'\bseamless(?:ly)?\b': 'smooth',
            r'\brobust\b': 'strong',
            r'\bcomprehensive\b': 'complete',

            # Gaming-specific AI tells
            r'\bembark(?:s|ed|ing)? on (?:a|an) (?:epic )?(?:journey|adventure)\b': 'start your adventure',
            r'\bmaster the art of\b': 'learn',
            r'\bunleash(?:s|ed|ing)? your potential\b': 'improve your skills',
            r'\belevate(?:s|d)? your (?:gameplay|experience)\b': 'improve your game',
        }

        # Phrases to remove entirely (hedging/filler)
        self.remove_phrases = [
            r"It'?s worth noting that ",
            r"It is worth noting that ",
            r"It'?s important to note that ",
            r"It is important to note that ",
            r"Generally speaking,? ",
            r"In today'?s digital landscape,? ",
            r"In the world of ",
            r"As we (?:all )?know,? ",
            r"At the end of the day,? ",
            r"The fact of the matter is,? ",
        ]

        # YouTube-specific removes
        self.youtube_intro_phrases = [
            r"^In this video,? (?:we'?ll|we will|I'?ll|I will) ",
            r"^Welcome to (?:this|my) (?:video|channel)[!.]?\s*",
            r"^Today,? (?:we'?re|we are|I'?m|I am) going to ",
            r"^(?:Hey|Hi) (?:guys|everyone|folks),?\s*",
        ]

        # Contractions for aggressive mode (formal -> casual)
        self.contractions = {
            r'\bdo not\b': "don't",
            r'\bdoes not\b': "doesn't",
            r'\bdid not\b': "didn't",
            r'\bcannot\b': "can't",
            r'\bwill not\b': "won't",
            r'\bshould not\b': "shouldn't",
            r'\bwould not\b': "wouldn't",
            r'\bcould not\b': "couldn't",
            r'\bmust not\b': "mustn't",
            r'\byou are\b': "you're",
            r'\bthey are\b': "they're",
            r'\bwe are\b': "we're",
            r'\bit is\b': "it's",
            r'\bthat is\b': "that's",
            r'\bwho is\b': "who's",
            r'\bwhat is\b': "what's",
            r'\bhere is\b': "here's",
            r'\bthere is\b': "there's",
            r'\bI am\b': "I'm",
            r'\byou will\b': "you'll",
            r'\bthey will\b': "they'll",
            r'\bwe will\b': "we'll",
            r'\bI will\b': "I'll",
        }

    def humanize(self, text: str, content_type: str = 'general') -> str:
        """
        Main entry point - apply all humanization filters

        Args:
            text: Input text to humanize
            content_type: Type of content being processed:
                - 'general': Standard text
                - 'youtube_title': YouTube video title
                - 'youtube_description': YouTube video description
                - 'youtube_tags': YouTube tags (minimal processing)

        Returns:
            Humanized text with AI telltale signs removed
        """
        if not text or not text.strip():
            return text

        # Apply transformations in order
        text = self.fix_punctuation_tells(text)
        text = self.replace_ai_word_choices(text)
        text = self.remove_hedging_language(text)

        # Content-type specific processing
        if content_type == 'youtube_title':
            text = self.fix_youtube_title_tells(text)
        elif content_type == 'youtube_description':
            text = self.fix_youtube_description_tells(text)
        elif content_type == 'youtube_tags':
            # Tags need minimal processing
            pass

        # Aggressive mode transformations
        if self.aggressive:
            text = self.add_contractions(text)

        # Final cleanup
        text = self.clean_whitespace(text)

        return text.strip()

    def fix_punctuation_tells(self, text: str) -> str:
        """
        Fix punctuation-based AI tells

        Handles:
        - Em dashes (—) -> periods with capitalization
        - Excessive punctuation (!!!, ???)
        - Multiple spaces
        """
        # Em dash replacement with proper capitalization
        if '—' in text:
            parts = text.split('—')
            cleaned_parts = []
            for i, part in enumerate(parts):
                part = part.strip()
                if part:
                    # Capitalize first letter of each part after the first
                    if i > 0 and part:
                        part = part[0].upper() + part[1:] if len(part) > 1 else part.upper()
                    cleaned_parts.append(part)
            text = '. '.join(cleaned_parts)

        # Excessive punctuation
        text = re.sub(r'[!]{2,}', '!', text)
        text = re.sub(r'[?]{2,}', '?', text)
        text = re.sub(r'[.]{3,}', '...', text)  # Preserve ellipsis but limit to 3

        return text

    def replace_ai_word_choices(self, text: str) -> str:
        """
        Replace common AI word choices with human alternatives

        Uses word boundary matching to avoid partial replacements.
        Case-insensitive matching with case preservation where possible.
        """
        for pattern, replacement in self.word_replacements.items():
            # Case-insensitive replacement
            text = re.sub(pattern, replacement, text, flags=re.IGNORECASE)

        return text

    def remove_hedging_language(self, text: str) -> str:
        """
        Remove unnecessary hedging and filler phrases that AI overuses

        These phrases add no value and make content sound robotic.
        """
        for phrase_pattern in self.remove_phrases:
            text = re.sub(phrase_pattern, '', text, flags=re.IGNORECASE)

        # After removing phrases, ensure sentence starts with capital letter
        text = text.strip()
        if text and text[0].islower():
            text = text[0].upper() + text[1:]

        return text

    def fix_youtube_title_tells(self, text: str) -> str:
        """
        Fix YouTube title-specific AI patterns

        Removes:
        - Excessive caps lock words
        - Overly generic adjectives
        - Multiple punctuation marks
        """
        # Remove excessive exclamation/question marks in titles
        text = re.sub(r'[!?]{2,}', '', text)

        # Remove all-caps marketing buzzwords if title is long enough
        if len(text) > 30:
            buzzwords = [
                r'\bEPIC\b',
                r'\bINCREDIBLE\b',
                r'\bAMAZING\b',
                r'\bUNBELIEVABLE\b',
                r'\bINSANE\b',
                r'\bCRAZY\b',
            ]
            for word in buzzwords:
                text = re.sub(word + r'\s*', '', text)

        # Remove generic adjectives that scream "AI-generated"
        if len(text) > 40:
            generic_adjectives = [
                r'\bultimate\b',
                r'\bcomplete\b',
                r'\bcomprehensive\b',
                r'\bdefinitive\b',
            ]
            for adj in generic_adjectives:
                text = re.sub(adj + r'\s+guide\b', 'guide', text, flags=re.IGNORECASE)

        return text

    def fix_youtube_description_tells(self, text: str) -> str:
        """
        Fix YouTube description-specific AI patterns

        Removes:
        - Generic video intros ("In this video...")
        - Formulaic welcomes
        - Obvious AI templates
        """
        for pattern in self.youtube_intro_phrases:
            text = re.sub(pattern, '', text, flags=re.IGNORECASE)

        # Remove "Don't forget to like and subscribe" variants (too generic)
        subscribe_patterns = [
            r"Don'?t forget to (?:like and )?subscribe[!.]?\s*",
            r"Make sure to (?:hit the )?(?:like button|subscribe)[!.]?\s*",
            r"Remember to (?:like and )?subscribe[!.]?\s*",
        ]
        for pattern in subscribe_patterns:
            text = re.sub(pattern, '', text, flags=re.IGNORECASE)

        # Capitalize first letter after removals
        text = text.strip()
        if text and text[0].islower():
            text = text[0].upper() + text[1:]

        return text

    def add_contractions(self, text: str) -> str:
        """
        Add contractions to make text more casual and human-sounding

        Only used in aggressive mode as it changes tone.
        AI tends to avoid contractions, making text sound formal.
        """
        for formal, casual in self.contractions.items():
            # Use word boundaries to avoid partial matches
            text = re.sub(formal, casual, text, flags=re.IGNORECASE)

        return text

    def clean_whitespace(self, text: str) -> str:
        """
        Clean up excessive whitespace and formatting issues

        Handles:
        - Multiple spaces -> single space
        - Space before sentence-ending punctuation
        - Multiple newlines

        Note: Deliberately avoids touching file extensions, URLs, code, etc.
        """
        # Multiple spaces to single space
        text = re.sub(r' {2,}', ' ', text)

        # Remove space before sentence-ending punctuation (! ? . , ;)
        # But ONLY if it's clearly at the end of a sentence (followed by space/newline/end)
        # This avoids breaking "demo_gui . py" -> "demo_gui.py"
        text = re.sub(r'\s+([!?.,;])\s+', r'\1 ', text)  # "word !" -> "word!"
        text = re.sub(r'\s+([!?.,;])$', r'\1', text, flags=re.MULTILINE)  # "word !" at line end

        # Ensure space after punctuation when followed by letter
        # But check it's not part of a file extension or URL
        def add_space_after_punct(match):
            punct = match.group(1)
            following = match.group(2)

            # Don't add space if this looks like a file extension or URL component
            # e.g., ".txt" or ".py" or ".com"
            if punct == '.' and following.islower() and len(following) <= 4:
                return match.group(0)  # Keep as-is

            return punct + ' ' + following

        text = re.sub(r'([.,!?;:])([A-Za-z])', add_space_after_punct, text)

        # Clean up multiple newlines (preserve paragraph breaks)
        text = re.sub(r'\n{3,}', '\n\n', text)

        return text


# ============================================================================
# Convenience Functions
# ============================================================================

def humanize_text(text: str, content_type: str = 'general', aggressive: bool = False) -> str:
    """
    Quick humanization of text - convenience function

    Args:
        text: Text to humanize
        content_type: Type of content:
            - 'general': Standard text processing
            - 'youtube_title': YouTube title optimization
            - 'youtube_description': YouTube description cleanup
            - 'youtube_tags': Minimal tag processing
        aggressive: Apply more transformations (adds contractions, more removals)

    Returns:
        Humanized text with AI telltale signs removed

    Examples:
        >>> humanize_text("Let's delve into this topic")
        "Let's explore this topic"

        >>> humanize_text("EPIC GAMING — it's worth noting that this is amazing!!!")
        "EPIC GAMING. This is amazing!"

        >>> humanize_text("In this video we will explore", content_type='youtube_description')
        "Explore"
    """
    humanizer = ContentHumanizer(aggressive=aggressive)
    return humanizer.humanize(text, content_type)


def humanize_title(title: str) -> str:
    """
    Humanize YouTube title specifically

    Args:
        title: YouTube video title

    Returns:
        Humanized title with AI tells removed
    """
    return humanize_text(title, content_type='youtube_title', aggressive=True)


def humanize_description(description: str) -> str:
    """
    Humanize YouTube description specifically

    Args:
        description: YouTube video description

    Returns:
        Humanized description with AI tells removed
    """
    return humanize_text(description, content_type='youtube_description', aggressive=False)


def humanize_tags(tags: str) -> str:
    """
    Humanize YouTube tags (minimal processing)

    Args:
        tags: YouTube tags string

    Returns:
        Cleaned tags
    """
    return humanize_text(tags, content_type='youtube_tags', aggressive=False)


# ============================================================================
# Analysis Functions (for debugging/testing)
# ============================================================================

def detect_ai_tells(text: str) -> List[Tuple[str, str]]:
    """
    Detect AI telltale signs in text (for analysis)

    Args:
        text: Text to analyze

    Returns:
        List of (pattern, matched_text) tuples for detected AI tells
    """
    tells = []

    # Check for common AI words
    ai_words = [
        'delve', 'leverage', 'utilize', 'moreover', 'furthermore',
        'unlock', 'harness', 'seamless', 'robust', 'comprehensive'
    ]
    for word in ai_words:
        pattern = r'\b' + word + r'(?:s|d|ing)?\b'
        matches = re.findall(pattern, text, flags=re.IGNORECASE)
        if matches:
            tells.extend([(word, match) for match in matches])

    # Check for em dashes
    if '—' in text:
        tells.append(('em_dash', text.count('—')))

    # Check for hedging phrases
    hedging = [
        "it's worth noting", "generally speaking",
        "in today's digital landscape"
    ]
    for phrase in hedging:
        if phrase in text.lower():
            tells.append(('hedging', phrase))

    return tells


def run_self_test():
    """Run self-test examples"""
    print("AI Content Humanizer - Self Test")
    print("=" * 70)

    test_cases = [
        {
            'name': 'Em dash replacement',
            'input': 'This is a test — and it continues here',
            'type': 'general'
        },
        {
            'name': 'AI word choices',
            'input': 'Let us delve into this topic and leverage our knowledge to utilize the tools',
            'type': 'general'
        },
        {
            'name': 'Hedging language',
            'input': "It's worth noting that this is important. Generally speaking, we should proceed.",
            'type': 'general'
        },
        {
            'name': 'YouTube title',
            'input': 'EPIC GAMING GUIDE — The ULTIMATE Comprehensive Tutorial!!!',
            'type': 'youtube_title'
        },
        {
            'name': 'YouTube description',
            'input': "In this video we'll explore the amazing world of gaming. Don't forget to subscribe!",
            'type': 'youtube_description'
        },
        {
            'name': 'Gaming AI tells',
            'input': 'Embark on an epic journey and master the art of combat to unleash your potential',
            'type': 'general'
        },
        {
            'name': 'Multiple tells',
            'input': "Moreover, it's worth noting that we should delve into this — it's a robust solution!!!",
            'type': 'general'
        }
    ]

    for test in test_cases:
        print(f"\nTest: {test['name']}")
        print(f"Input:  {test['input']}")
        result = humanize_text(test['input'], content_type=test['type'])
        print(f"Output: {result}")

        # Show detected tells
        tells = detect_ai_tells(test['input'])
        if tells:
            print(f"Detected AI tells: {tells}")

    print("\n" + "=" * 70)
    print("Self-test complete!")


if __name__ == "__main__":
    import argparse
    import sys
    from pathlib import Path

    parser = argparse.ArgumentParser(
        description='AI Content Humanizer - Remove AI telltale signs from text',
        epilog='''
Examples:
  python yt_humanizer.py file.txt                    # Humanize and overwrite file.txt
  python yt_humanizer.py input.txt output.txt        # Humanize input.txt, save to output.txt
  python yt_humanizer.py file.txt --aggressive       # Use aggressive mode (adds contractions)
  python yt_humanizer.py file.txt --type title       # Treat as YouTube title
  python yt_humanizer.py --test                      # Run self-tests
  python yt_humanizer.py --analyze file.txt          # Detect AI tells without humanizing
        ''',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument('input_file', nargs='?', help='Input file to humanize')
    parser.add_argument('output_file', nargs='?', help='Output file (optional, defaults to overwriting input)')
    parser.add_argument('--aggressive', '-a', action='store_true',
                       help='Use aggressive mode (adds contractions)')
    parser.add_argument('--type', '-t', choices=['general', 'title', 'description', 'tags'],
                       default='general',
                       help='Content type (default: general)')
    parser.add_argument('--test', action='store_true',
                       help='Run self-tests')
    parser.add_argument('--analyze', action='store_true',
                       help='Analyze file for AI tells without humanizing')

    args = parser.parse_args()

    # Run self-test if requested
    if args.test:
        run_self_test()
        sys.exit(0)

    # Require input file for other operations
    if not args.input_file:
        parser.print_help()
        sys.exit(1)

    input_path = Path(args.input_file)

    # Check if input file exists
    if not input_path.exists():
        print(f"Error: Input file not found: {input_path}")
        sys.exit(1)

    # Read input file
    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            text = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    # Analyze mode - just detect AI tells
    if args.analyze:
        print(f"Analyzing: {input_path}")
        print("=" * 70)
        tells = detect_ai_tells(text)
        if tells:
            print(f"\nDetected {len(tells)} AI telltale signs:")
            for pattern, match in tells:
                print(f"  - {pattern}: {match}")
        else:
            print("\nNo obvious AI tells detected!")
        print("=" * 70)
        sys.exit(0)

    # Humanize mode
    print(f"Humanizing: {input_path}")
    print(f"Mode: {'Aggressive' if args.aggressive else 'Normal'}")
    print(f"Type: {args.type}")

    # Map type argument to content_type
    content_type_map = {
        'general': 'general',
        'title': 'youtube_title',
        'description': 'youtube_description',
        'tags': 'youtube_tags'
    }
    content_type = content_type_map[args.type]

    # Humanize text
    humanized = humanize_text(text, content_type=content_type, aggressive=args.aggressive)

    # Determine output file
    output_path = Path(args.output_file) if args.output_file else input_path

    # Write output
    try:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(humanized)

        if output_path == input_path:
            print(f"File updated: {output_path}")
        else:
            print(f"Output saved: {output_path}")

        # Show statistics
        original_length = len(text)
        humanized_length = len(humanized)
        diff = original_length - humanized_length

        print(f"\nStatistics:")
        print(f"  Original: {original_length} characters")
        print(f"  Humanized: {humanized_length} characters")
        if diff != 0:
            print(f"  Difference: {diff:+d} characters")

        # Detect and show what was changed
        tells = detect_ai_tells(text)
        if tells:
            print(f"\nRemoved {len(tells)} AI telltale signs")

    except Exception as e:
        print(f"Error writing file: {e}")
        sys.exit(1)
